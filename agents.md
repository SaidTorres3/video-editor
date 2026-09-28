**Instructions — video-editor**

Scope: repository-wide (applies to building and CI steps on Windows)

Instruction:

- When building the project, always run the PowerShell script with the `-Static` flag:

  - `.\run.ps1 -Static`

Why:

- Ensures a consistent, static build invocation and matches local developer workflows.

Examples / Prompts:

- "Run the project's build script: `.\run.ps1 -Static`"
- "When I ask you to build or describe build steps, include `.\run.ps1 -Static` as the command."

Clarifying questions / Notes:

- Should this instruction be enforced only on Windows/PowerShell environments, or should we add platform-specific alternatives for other OSes?
- If you want this enforced in CI, I can add a sample CI snippet that calls `.\run.ps1 -Static`.
