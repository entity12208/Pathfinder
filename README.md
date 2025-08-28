# Pathfinder (Universal Geometry Dash Levels)

Pathfinder now supports **all Geometry Dash levels, versions, and objects**. No configuration or selection is needed: just run the mod and it will attempt to solve any level using the latest physics, objects, triggers, and mechanics.

## Features

- **Full GD Level Support**: All player forms, objects, triggers, portals, speeds, rotations, and custom objects.
- **Universal Level Parsing**: Automatically adapts to any level format or version.
- **Advanced Physics Simulation**: Realistic, extensible engine for all game mechanics.
- **Automatic Macro Generation**: Macros exported for any level, ready for playback with compatible bots.
- **Performance & Robustness**: Fast simulation, modular code, easy debugging.

## Usage

1. Open any Geometry Dash level.
2. Click the Pathfinder button.
3. Wait for simulation to complete.
4. Export the generated macro and play with your bot.

## Developer Notes

- Uses [Geode SDK](https://docs.geode-sdk.org/) for modding.
- All code modularized for easy extension and updates.
- All logic is generic: any new object or mechanic will be simulated as best as possible.
- For new game updates, simply add new behaviors to `/src/gd_object_behaviors.hpp`.

## Future Improvements

- Smarter input generation (A*, genetic, RL, etc.).
- More accurate custom object and trigger simulation.
- Real-time error reporting and feature logging.

Contributions welcome!
