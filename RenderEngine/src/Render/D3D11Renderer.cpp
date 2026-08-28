#include "Render/D3D11Renderer.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <array>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace {

struct Vertex final {
    float position[3];
    float color[3];
};

// Three visible faces of one isometric voxel. This proves the full rendering
// path before the next milestone adds automatic chunk-mesh generation.
constexpr std::array<Vertex, 18> voxel_vertices{{
    {{ 0.00F,  0.55F, 0.0F}, {0.45F, 0.95F, 0.72F}},
    {{-0.42F,  0.28F, 0.0F}, {0.34F, 0.82F, 0.62F}},
    {{ 0.00F,  0.02F, 0.0F}, {0.28F, 0.74F, 0.56F}},
    {{ 0.00F,  0.55F, 0.0F}, {0.45F, 0.95F, 0.72F}},
    {{ 0.00F,  0.02F, 0.0F}, {0.28F, 0.74F, 0.56F}},
    {{ 0.42F,  0.28F, 0.0F}, {0.39F, 0.88F, 0.66F}},

    {{-0.42F,  0.28F, 0.0F}, {0.18F, 0.55F, 0.42F}},
    {{-0.42F, -0.25F, 0.0F}, {0.12F, 0.39F, 0.31F}},
    {{ 0.00F, -0.52F, 0.0F}, {0.10F, 0.34F, 0.27F}},
    {{-0.42F,  0.28F, 0.0F}, {0.18F, 0.55F, 0.42F}},
    {{ 0.00F, -0.52F, 0.0F}, {0.10F, 0.34F, 0.27F}},
    {{ 0.00F,  0.02F, 0.0F}, {0.15F, 0.48F, 0.37F}},

    {{ 0.00F,  0.02F, 0.0F}, {0.20F, 0.63F, 0.78F}},
    {{ 0.00F, -0.52F, 0.0F}, {0.12F, 0.43F, 0.58F}},
    {{ 0.42F, -0.25F, 0.0F}, {0.14F, 0.48F, 0.64F}},
    {{ 0.00F,  0.02F, 0.0F}, {0.20F, 0.63F, 0.78F}},
    {{ 0.42F, -0.25F, 0.0F}, {0.14F, 0.48F, 0.64F}},
    {{ 0.42F,  0.28F, 0.0F}, {0.24F, 0.71F, 0.84F}},
}};

constexpr char vertex_shader_source[] = R"(
struct VSInput { float3 position : POSITION; float3 color : COLOR; };
struct PSInput { float4 position : SV_POSITION; float3 color : COLOR; };
PSInput main(VSInput input) {
    PSInput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
})";

constexpr char pixel_shader_source[] = R"(
struct PSInput { float4 position : SV_POSITION; float3 color : COLOR; };
float4 main(PSInput input) : SV_TARGET { return float4(input.color, 1.0); }
)";

class VisualDemo final {
public:
    bool run() noexcept {
        if (!create_window() || !create_graphics()) {
            cleanup();
            return false;
        }

        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
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
        if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rectangle{0, 0, 1280, 720};
        AdjustWindowRect(&rectangle, style, FALSE);
        window_ = CreateWindowExW(0, window_class.lpszClassName, L"MC-Redux - First Voxel (DirectX 11)",
            style, CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left,
            rectangle.bottom - rectangle.top, nullptr, nullptr, instance_, nullptr);
        return window_ != nullptr;
    }

    bool create_graphics() noexcept {
        DXGI_SWAP_CHAIN_DESC swap_description{};
        swap_description.BufferDesc.Width = 1280;
        swap_description.BufferDesc.Height = 720;
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
            || FAILED(device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target_))) return false;

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
                                 compile_flags, 0, &pixel_bytecode, &errors))) return false;
        if (FAILED(device_->CreateVertexShader(vertex_bytecode->GetBufferPointer(),
                                               vertex_bytecode->GetBufferSize(), nullptr,
                                               &vertex_shader_))
            || FAILED(device_->CreatePixelShader(pixel_bytecode->GetBufferPointer(),
                                                 pixel_bytecode->GetBufferSize(), nullptr,
                                                 &pixel_shader_))) return false;

        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> input_elements{{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        }};
        if (FAILED(device_->CreateInputLayout(input_elements.data(),
                                              static_cast<UINT>(input_elements.size()),
                                              vertex_bytecode->GetBufferPointer(),
                                              vertex_bytecode->GetBufferSize(), &input_layout_))) return false;

        D3D11_BUFFER_DESC vertex_description{};
        vertex_description.ByteWidth = static_cast<UINT>(sizeof(voxel_vertices));
        vertex_description.Usage = D3D11_USAGE_IMMUTABLE;
        vertex_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertex_data{};
        vertex_data.pSysMem = voxel_vertices.data();
        if (FAILED(device_->CreateBuffer(&vertex_description, &vertex_data, &vertex_buffer_))) return false;

        D3D11_RASTERIZER_DESC rasterizer_description{};
        rasterizer_description.FillMode = D3D11_FILL_SOLID;
        rasterizer_description.CullMode = D3D11_CULL_NONE;
        rasterizer_description.DepthClipEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(&rasterizer_description, &rasterizer_))) return false;

        viewport_.TopLeftX = 0.0F;
        viewport_.TopLeftY = 0.0F;
        viewport_.Width = 1280.0F;
        viewport_.Height = 720.0F;
        viewport_.MinDepth = 0.0F;
        viewport_.MaxDepth = 1.0F;
        return true;
    }

    bool render() noexcept {
        constexpr float clear_color[4]{0.025F, 0.035F, 0.065F, 1.0F};
        context_->ClearRenderTargetView(render_target_.Get(), clear_color);
        ID3D11RenderTargetView* targets[]{render_target_.Get()};
        context_->OMSetRenderTargets(1, targets, nullptr);
        context_->RSSetViewports(1, &viewport_);
        context_->RSSetState(rasterizer_.Get());

        constexpr UINT stride = sizeof(Vertex);
        constexpr UINT offset = 0;
        ID3D11Buffer* buffers[]{vertex_buffer_.Get()};
        context_->IASetInputLayout(input_layout_.Get());
        context_->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
        context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
        context_->Draw(static_cast<UINT>(voxel_vertices.size()), 0);
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
    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> pixel_shader_;
    ComPtr<ID3D11InputLayout> input_layout_;
    ComPtr<ID3D11Buffer> vertex_buffer_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    D3D11_VIEWPORT viewport_{};
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
    return "DirectX 11 voxel renderer";
}

} // namespace mcr::render
