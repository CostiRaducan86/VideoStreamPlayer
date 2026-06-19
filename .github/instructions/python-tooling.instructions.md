---
name: Python tooling rules
description: Rules for Python tools, camera scripts, SDK/API integration and analysis scripts.
applyTo: "**/*.py"
---

# Python tooling rules

- Use a clear `main()` entry point.
- Add type hints for new public functions.
- Avoid hard-coded absolute paths.
- Handle missing camera/API/SDK resources with clear errors.
- Cleanup resources safely on exit.
- Keep acquisition and image-processing parameters named and easy to tune.
- If adding dependencies, document the install command or update the dependency file.
