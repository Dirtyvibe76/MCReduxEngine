#include "Render/D3D11Renderer.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <DirectXMath.h>

#include "World/Chunk/Chunk.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {

constexpr UINT window_width = 1280;
constexpr UINT window_height = 720;

struct Vertex final {
    float position[3];
    float color[3];
};

struct SceneConstants final {
    XMFLOAT4X4 view_projection;
};

struct Face final {
    int neighbor_x;
    int neighbor_y;
    int neighbor_z;
    float shade;
    std::array<std::array<float, 3>, 4> corners;
};

constexpr std::array<Face, 6> block_faces{{
    { 1,  0,  0, 0.82F, {{{1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F},
                           {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F, 1.0F}}}},
    {-1,  0,  0, 0.68F, {{{0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 1.0F},
                           {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F}}}},
    { 0,  1,  0, 1.00F, {{{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 1.0F},
                           {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 0.0F}}}},
    { 0, -1,  0, 0.48F, {{{0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F},
                           {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 1.0F}}}},
    { 0,  0,  1, 0.76F, {{{1.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F},
                           {0.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 1.0F}}}},
    { 0,  0, -1, 0.60F, {{{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
                           {1.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}}},
}};

constexpr char vertex_shader_source[] = R"(
cbuffer SceneConstants : register(b0) { matrix viewProjection; };
struct VSInput { float3 position : POSITION; float3 color : COLOR; };
struct PSInput { float4 position : SV_POSITION; float3 color : COLOR; };
PSInput main(VSInput input) {
    PSInput output;
    output.position = mul(float4(input.position, 1.0), viewProjection);
    output.color = input.color;
    return output;
})";

constexpr char pixel_shader_source[] = R"(
struct PSInput { float4 position : SV_POSITION; float3 color : COLOR; };
float4 main(PSInput input) : SV_TARGET { return float4(input.color, 1.0); }
)";

int terrain_height(const int x, const int z) noexcept {
    const float rolling = std::sin(static_cast<float>(x) * 0.48F) * 1.5F
        + std::cos(static_cast<float>(z) * 0.41F) * 1.35F
        + std::sin(static_cast<float>(x + z) * 0.23F) * 0.8F;
    return std::clamp(5 + static_cast<int>(std::round(rolling)), 2, 9);
}

mcr::world::Chunk generate_demo_chunk() {
    mcr::world::Chunk chunk{{0, 0}};
    for (int z = 0; z < mcr::world::Chunk::width; ++z) {
        for (int x = 0; x < mcr::world::Chunk::width; ++x) {
            const int surface = terrain_height(x, z);
            for (int y = 0; y <= surface; ++y) {
                const mcr::world::Chunk::BlockId block =
                    y == surface ? 1 : (y >= surface - 2 ? 2 : 3);
                chunk.set_block(x, y, z, block);
            }
        }
    }
    return chunk;
}

bool is_solid(const mcr::world::Chunk& chunk, const int x, const int y, const int z) noexcept {
    if (x < 0 || x >= mcr::world::Chunk::width || y < 0 || y >= mcr::world::Chunk::height
        || z < 0 || z >= mcr::world::Chunk::width) return false;
    return chunk.block(x, y, z) != 0;
}

std::array<float, 3> block_color(const mcr::world::Chunk::BlockId block) noexcept {
    switch (block) {
    case 1: return {0.24F, 0.72F, 0.34F};
    case 2: return {0.48F, 0.30F, 0.16F};
    default: return {0.43F, 0.46F, 0.50F};
    }
}

std::vector<Vertex> mesh_chunk(const mcr::world::Chunk& chunk) {
    std::vector<Vertex> vertices;
    vertices.reserve(12000);
    constexpr std::array<unsigned, 6> triangle_indices{0, 1, 2, 0, 2, 3};

    for (int y = 0; y < mcr::world::Chunk::height; ++y) {
        for (int z = 0; z < mcr::world::Chunk::width; ++z) {
            for (int x = 0; x < mcr::world::Chunk::width; ++x) {
                const auto block = chunk.block(x, y, z);
                if (block == 0) continue;
                const auto base_color = block_color(block);
                const float variation =
                    0.92F + static_cast<float>((x * 17 + y * 7 + z * 13) & 7) * 0.012F;

                for (const auto& face : block_faces) {
                    if (is_solid(chunk, x + face.neighbor_x, y + face.neighbor_y,
                                 z + face.neighbor_z)) continue;
                    for (const unsigned corner_index : triangle_indices) {
                        const auto& corner = face.corners[corner_index];
                        vertices.push_back({
                            {static_cast<float>(x) + corner[0], static_cast<float>(y) + corner[1],
                             static_cast<float>(z) + corner[2]},
                            {base_color[0] * face.shade * variation,
                             base_color[1] * face.shade * variation,
                             base_color[2] * face.shade * variation}
                        });
                    }
                }
            }
        }
    }
    return vertices;
}

class VisualDemo final {
public:
    bool run() noexcept {
        if (!create_window() || !create_graphics()) {
            cleanup();
            return false;
        }

        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        previous_frame_ = std::chrono::steady_clock::now();
        MSG message{};
        while (message.message != WM_QUIT) {
            if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            } else if (!render()) {
                cleanup();
                return false;
            }
        }
        cleanup();
        return true;
    }

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    bool create_window() noexcept {
        instance_ = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = L"MCReduxD3D11Window";
        if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;

        constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rectangle{0, 0, static_cast<LONG>(window_width), static_cast<LONG>(window_height)};
        AdjustWindowRect(&rectangle, style, FALSE);
        window_ = CreateWindowExW(0, window_class.lpszClassName,
            L"MC-Redux - Voxel Chunk + Fly Camera (DirectX 11)", style,
            CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left,
            rectangle.bottom - rectangle.top, nullptr, nullptr, instance_, nullptr);
        return window_ != nullptr;
    }

    bool create_graphics() noexcept {
        DXGI_SWAP_CHAIN_DESC swap_description{};
        swap_description.BufferDesc.Width = window_width;
        swap_description.BufferDesc.Height = window_height;
        swap_description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_description.SampleDesc.Count = 1;
        swap_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_description.BufferCount = 2;
        swap_description.OutputWindow = window_;
        swap_description.Windowed = TRUE;
        swap_description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr D3D_FEATURE_LEVEL requested_levels[]{D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL created_level{};
        HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            requested_levels, 1, D3D11_SDK_VERSION, &swap_description,
            &swap_chain_, &device_, &created_level, &context_);
        if (FAILED(result)) {
            result = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                requested_levels, 1, D3D11_SDK_VERSION, &swap_description,
                &swap_chain_, &device_, &created_level, &context_);
        }
        if (FAILED(result) || created_level < D3D_FEATURE_LEVEL_11_0) return false;

        ComPtr<ID3D11Texture2D> back_buffer;
        if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))
            || FAILED(device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target_)))
            return false;

        D3D11_TEXTURE2D_DESC depth_description{};
        depth_description.Width = window_width;
        depth_description.Height = window_height;
        depth_description.MipLevels = 1;
        depth_description.ArraySize = 1;
        depth_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depth_description.SampleDesc.Count = 1;
        depth_description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(device_->CreateTexture2D(&depth_description, nullptr, &depth_texture_))
            || FAILED(device_->CreateDepthStencilView(depth_texture_.Get(), nullptr, &depth_view_)))
            return false;

        ComPtr<ID3DBlob> vertex_bytecode;
        ComPtr<ID3DBlob> pixel_bytecode;
        ComPtr<ID3DBlob> errors;
        UINT compile_flags = 0;
#if defined(_DEBUG)
        compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        if (FAILED(D3DCompile(vertex_shader_source, sizeof(vertex_shader_source) - 1,
                              nullptr, nullptr, nullptr, "main", "vs_5_0",
                              compile_flags, 0, &vertex_bytecode, &errors))
            || FAILED(D3DCompile(pixel_shader_source, sizeof(pixel_shader_source) - 1,
                                 nullptr, nullptr, nullptr, "main", "ps_5_0",
                                 compile_flags, 0, &pixel_bytecode, &errors)))
            return false;
        if (FAILED(device_->CreateVertexShader(vertex_bytecode->GetBufferPointer(),
                                               vertex_bytecode->GetBufferSize(), nullptr,
                                               &vertex_shader_))
            || FAILED(device_->CreatePixelShader(pixel_bytecode->GetBufferPointer(),
                                                 pixel_bytecode->GetBufferSize(), nullptr,
                                                 &pixel_shader_)))
            return false;

        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> input_elements{{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
        }};
        if (FAILED(device_->CreateInputLayout(input_elements.data(),
                                              static_cast<UINT>(input_elements.size()),
                                              vertex_bytecode->GetBufferPointer(),
                                              vertex_bytecode->GetBufferSize(),
                                              &input_layout_)))
            return false;

        const auto chunk = generate_demo_chunk();
        const auto vertices = mesh_chunk(chunk);
        if (vertices.empty()) return false;
        vertex_count_ = static_cast<UINT>(vertices.size());
        D3D11_BUFFER_DESC vertex_description{};
        vertex_description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
        vertex_description.Usage = D3D11_USAGE_IMMUTABLE;
        vertex_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertex_data{};
        vertex_data.pSysMem = vertices.data();
        if (FAILED(device_->CreateBuffer(&vertex_description, &vertex_data, &vertex_buffer_)))
            return false;

        D3D11_BUFFER_DESC constant_description{};
        constant_description.ByteWidth = sizeof(SceneConstants);
        constant_description.Usage = D3D11_USAGE_DEFAULT;
        constant_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(&constant_description, nullptr, &scene_constants_)))
            return false;

        D3D11_RASTERIZER_DESC rasterizer_description{};
        rasterizer_description.FillMode = D3D11_FILL_SOLID;
        rasterizer_description.CullMode = D3D11_CULL_NONE;
        rasterizer_description.DepthClipEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(&rasterizer_description, &rasterizer_)))
            return false;

        viewport_.TopLeftX = 0.0F;
        viewport_.TopLeftY = 0.0F;
        viewport_.Width = static_cast<float>(window_width);
        viewport_.Height = static_cast<float>(window_height);
        viewport_.MinDepth = 0.0F;
        viewport_.MaxDepth = 1.0F;
        return true;
    }

    void update_camera(const float delta_seconds) noexcept {
        if (GetForegroundWindow() != window_) return;

        POINT cursor{};
        GetCursorPos(&cursor);
        const bool right_mouse_down = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (right_mouse_down) {
            if (mouse_looking_) {
                yaw_ += static_cast<float>(cursor.x - previous_cursor_.x) * 0.004F;
                pitch_ -= static_cast<float>(cursor.y - previous_cursor_.y) * 0.004F;
                pitch_ = std::clamp(pitch_, -1.45F, 1.45F);
            }
            previous_cursor_ = cursor;
            mouse_looking_ = true;
        } else {
            mouse_looking_ = false;
        }

        const XMVECTOR flat_forward = XMVector3Normalize(
            XMVectorSet(std::sin(yaw_), 0.0F, std::cos(yaw_), 0.0F));
        const XMVECTOR right = XMVector3Normalize(
            XMVector3Cross(XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F), flat_forward));
        XMVECTOR movement = XMVectorZero();
        if (GetAsyncKeyState('W') & 0x8000) movement = XMVectorAdd(movement, flat_forward);
        if (GetAsyncKeyState('S') & 0x8000) movement = XMVectorSubtract(movement, flat_forward);
        if (GetAsyncKeyState('D') & 0x8000) movement = XMVectorAdd(movement, right);
        if (GetAsyncKeyState('A') & 0x8000) movement = XMVectorSubtract(movement, right);
        if (GetAsyncKeyState('E') & 0x8000)
            movement = XMVectorAdd(movement, XMVectorSet(0, 1, 0, 0));
        if (GetAsyncKeyState('Q') & 0x8000)
            movement = XMVectorSubtract(movement, XMVectorSet(0, 1, 0, 0));

        if (XMVectorGetX(XMVector3LengthSq(movement)) > 0.0F) {
            const float speed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 18.0F : 7.0F;
            const XMVECTOR position = XMLoadFloat3(&camera_position_);
            XMStoreFloat3(&camera_position_, XMVectorAdd(position,
                XMVectorScale(XMVector3Normalize(movement), speed * delta_seconds)));
        }
    }

    bool render() noexcept {
        const auto now = std::chrono::steady_clock::now();
        const float delta_seconds =
            std::min(std::chrono::duration<float>(now - previous_frame_).count(), 0.1F);
        previous_frame_ = now;
        update_camera(delta_seconds);

        const XMVECTOR position = XMLoadFloat3(&camera_position_);
        const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
            std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
            std::cos(pitch_) * std::cos(yaw_), 0.0F));
        const XMMATRIX view =
            XMMatrixLookToLH(position, forward, XMVectorSet(0, 1, 0, 0));
        const XMMATRIX projection = XMMatrixPerspectiveFovLH(
            XMConvertToRadians(65.0F),
            static_cast<float>(window_width) / static_cast<float>(window_height),
            0.1F, 250.0F);
        SceneConstants constants{};
        XMStoreFloat4x4(&constants.view_projection,
                        XMMatrixTranspose(view * projection));
        context_->UpdateSubresource(scene_constants_.Get(), 0, nullptr, &constants, 0, 0);

        constexpr float clear_color[4]{0.025F, 0.035F, 0.065F, 1.0F};
        context_->ClearRenderTargetView(render_target_.Get(), clear_color);
        context_->ClearDepthStencilView(
            depth_view_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0F, 0);
        ID3D11RenderTargetView* targets[]{render_target_.Get()};
        context_->OMSetRenderTargets(1, targets, depth_view_.Get());
        context_->RSSetViewports(1, &viewport_);
        context_->RSSetState(rasterizer_.Get());

        constexpr UINT stride = sizeof(Vertex);
        constexpr UINT offset = 0;
        ID3D11Buffer* buffers[]{vertex_buffer_.Get()};
        ID3D11Buffer* constants_buffer[]{scene_constants_.Get()};
        context_->IASetInputLayout(input_layout_.Get());
        context_->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, constants_buffer);
        context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
        context_->Draw(vertex_count_, 0);
        return SUCCEEDED(swap_chain_->Present(1, 0));
    }

    void cleanup() noexcept {
        if (context_) context_->ClearState();
        if (window_ && IsWindow(window_)) DestroyWindow(window_);
        window_ = nullptr;
    }

    HINSTANCE instance_{nullptr};
    HWND window_{nullptr};
    ComPtr<IDXGISwapChain> swap_chain_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11RenderTargetView> render_target_;
    ComPtr<ID3D11Texture2D> depth_texture_;
    ComPtr<ID3D11DepthStencilView> depth_view_;
    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> pixel_shader_;
    ComPtr<ID3D11InputLayout> input_layout_;
    ComPtr<ID3D11Buffer> vertex_buffer_;
    ComPtr<ID3D11Buffer> scene_constants_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    D3D11_VIEWPORT viewport_{};
    UINT vertex_count_{0};
    XMFLOAT3 camera_position_{8.0F, 11.0F, -16.0F};
    float yaw_{0.0F};
    float pitch_{-0.22F};
    bool mouse_looking_{false};
    POINT previous_cursor_{};
    std::chrono::steady_clock::time_point previous_frame_{};
};

} // namespace
#endif

namespace mcr::render {

bool D3D11Renderer::initialize() noexcept {
    ready_ = true;
    return true;
}

bool D3D11Renderer::run_visual_demo() noexcept {
#ifdef _WIN32
    VisualDemo demo;
    return demo.run();
#else
    return true;
#endif
}

void D3D11Renderer::shutdown() noexcept { ready_ = false; }

std::string_view D3D11Renderer::backend_name() const noexcept {
    return "DirectX 11 chunk renderer";
}

} // namespace mcr::render
