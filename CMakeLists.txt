cmake_minimum_required(VERSION 3.22)
project(MCReduxEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(CoreEngine
    CoreEngine/src/Core/ECS/Entity.cpp
    CoreEngine/src/Core/ECS/System.cpp
    CoreEngine/src/Core/EventBus/EventBus.cpp
    CoreEngine/src/Core/Time/GameLoop.cpp
    CoreEngine/src/Core/Math/Noise.cpp)
target_include_directories(CoreEngine PUBLIC CoreEngine/include)

add_library(WorldEngine
    WorldEngine/src/World/Chunk/Chunk.cpp
    WorldEngine/src/World/Chunk/ChunkManager.cpp)
target_include_directories(WorldEngine PUBLIC WorldEngine/include)
target_link_libraries(WorldEngine PUBLIC CoreEngine)

add_library(PhysicsEngine
    PhysicsEngine/src/Physics/PhysicsSystem.cpp
    PhysicsEngine/src/Physics/CollisionResolver.cpp)
target_include_directories(PhysicsEngine PUBLIC PhysicsEngine/include)
target_link_libraries(PhysicsEngine PUBLIC CoreEngine WorldEngine)

add_library(CombatEngine CombatEngine/src/Combat/CombatSystem.cpp)
target_include_directories(CombatEngine PUBLIC CombatEngine/include)
target_link_libraries(CombatEngine PUBLIC CoreEngine PhysicsEngine)

add_library(AIEngine AIEngine/src/AI/StateMachine.cpp)
target_include_directories(AIEngine PUBLIC AIEngine/include)
target_link_libraries(AIEngine PUBLIC CoreEngine WorldEngine CombatEngine)

add_library(WorldgenEngine WorldgenEngine/src/Worldgen/Generators.cpp)
target_include_directories(WorldgenEngine PUBLIC WorldgenEngine/include)
target_link_libraries(WorldgenEngine PUBLIC CoreEngine WorldEngine AIEngine)

add_library(RenderEngine RenderEngine/src/Render/D3D11Renderer.cpp)
target_include_directories(RenderEngine PUBLIC RenderEngine/include)
target_link_libraries(RenderEngine PUBLIC CoreEngine WorldEngine)

add_library(MCReduxGame
    MCReduxGame/src/Game/Game.cpp
    MCReduxGame/src/Game/StaminaSystem.cpp
    MCReduxGame/src/Game/ToolEfficiencySystem.cpp)
target_include_directories(MCReduxGame PUBLIC MCReduxGame/include)
target_link_libraries(MCReduxGame PUBLIC CoreEngine WorldEngine PhysicsEngine CombatEngine AIEngine WorldgenEngine RenderEngine)

add_executable(MCReduxEngine MCReduxEngine/main.cpp)
target_link_libraries(MCReduxEngine PRIVATE MCReduxGame)

enable_testing()
add_test(NAME engine_smoke_test COMMAND MCReduxEngine)

# Deploy renderer assets beside the executable so runtime loading works
# for Visual Studio/CMake Debug and Release builds.
add_custom_command(TARGET MCReduxEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:MCReduxEngine>/assets/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/RenderEngine/assets/textures/block_albedo.png"
        "$<TARGET_FILE_DIR:MCReduxEngine>/assets/textures/block_albedo.png"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/RenderEngine/assets/textures/pick_albedo.png"
        "$<TARGET_FILE_DIR:MCReduxEngine>/assets/textures/pick_albedo.png"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/RenderEngine/assets/textures/pick_normal.png"
        "$<TARGET_FILE_DIR:MCReduxEngine>/assets/textures/pick_normal.png"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/RenderEngine/assets/textures/pick_roughness.png"
        "$<TARGET_FILE_DIR:MCReduxEngine>/assets/textures/pick_roughness.png"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/RenderEngine/assets/textures/pick_metallic.png"
        "$<TARGET_FILE_DIR:MCReduxEngine>/assets/textures/pick_metallic.png"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/RenderEngine/assets/textures/arm_albedo.png"
        "$<TARGET_FILE_DIR:MCReduxEngine>/assets/textures/arm_albedo.png"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/RenderEngine/assets/textures/arm_normal.png"
        "$<TARGET_FILE_DIR:MCReduxEngine>/assets/textures/arm_normal.png"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/RenderEngine/assets/textures/arm_roughness.png"
        "$<TARGET_FILE_DIR:MCReduxEngine>/assets/textures/arm_roughness.png"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/RenderEngine/assets/textures/arm_metallic.png"
        "$<TARGET_FILE_DIR:MCReduxEngine>/assets/textures/arm_metallic.png"

    COMMENT "Copying MC-Redux runtime assets"
)
