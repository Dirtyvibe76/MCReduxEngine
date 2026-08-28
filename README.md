# MC-Redux Engine Foundation

This solution is the first compiling foundation for the standalone MC-Redux engine.

## Open and run in Visual Studio

1. Open `MCReduxEngine.slnx`.
2. Choose `Debug` and `x64` in the toolbar.
3. Right-click the `MCReduxEngine` project and select **Set as Startup Project**.
4. Select **Build > Build Solution**.
5. Press **Ctrl+F5**.

The first run performs the engine smoke test and then opens a real DirectX 11
window displaying a generated and exposed-face-meshed 16x256x16 voxel chunk.

## Camera controls

- `W`, `A`, `S`, `D`: move horizontally.
- `Q` / `E`: descend / ascend.
- Hold the right mouse button and drag: look around.
- Hold `Shift`: move faster.
- `Esc`: exit.

## Project roles

- `CoreEngine`: entity/component registry, event bus, fixed tick loop, math.
- `WorldEngine`: chunks, biome specifications, and structure specifications.
- `PhysicsEngine`: AABBs, collision checks, and deterministic movement.
- `CombatEngine`: hit/hurt boxes, frame data, and damage packets.
- `AIEngine`: reusable state-machine foundation.
- `WorldgenEngine`: deterministic biome, mob, boss, and structure specs.
- `RenderEngine`: DirectX 11 device, depth buffer, chunk mesher, shaders, and fly camera.
- `MCReduxGame`: game-layer integration and player state.
- `MCReduxEngine`: executable launcher and smoke test.

## Important boundary

This is an engine foundation, not the completed game. The large design document
contains mechanics that must be implemented and tested incrementally. The next
milestone is integrating chunk streaming and remeshing when blocks change.
