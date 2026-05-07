# Agent Scope Instructions

When working on this project, Cascade agents should:

- **Prioritize files in `src/build/`** for primary analysis and code inspection
- **Only reference files outside `src/build/`** (e.g., `include/`, `CMakeLists.txt`, `build.bat`) when explicitly necessary for the task at hand
- Maintain this scope restriction across all agent interactions in this project

This ensures focused attention on the active build artifacts and source code while allowing escape to configuration files when needed for context.
