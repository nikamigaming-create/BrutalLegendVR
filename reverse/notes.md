# Raw Reverse Engineering Notes

- Game uses Double Fine Buddha Engine.
- Shaders are D3D9 bytecode.
- View and Projection matrices are computed inside Camera::UpdateWorldTransform and passed to shaders.
- Default FOV is 45.0 degrees.
- Direct3DCreate9 called at 0x6536cd with D3D_SDK_VERSION=32.
- Device creation: IDirect3D9::CreateDevice is invoked during startup.
- Fullscreen/windowed setting controlled by D3DPRESENT_PARAMETERS::Windowed in CreateDevice.
