# Repository Guidelines

## Project Structure & Module Organization
Current top-level layout:
- `firmware/` ESP-IDF firmware for BMI270 + UDP streaming
- `pc/` host-side Python utilities
- `scripts/` developer helper scripts
- `docs/` workflow and design notes
- `data/` runtime artifacts (raw CSV, label manifest, built model), gitignored

## Build, Test, and Development Commands
No build or test commands are defined yet. Once tooling is added, list the canonical commands and what they do, for example:
- `npm run build` — compile assets for production
- `npm test` — run the test suite
- `npm run dev` — start a local dev server

## Coding Style & Naming Conventions
No coding standards are defined yet. When setting up the project, specify:
- Indentation (e.g., 2 or 4 spaces)
- Naming (e.g., `camelCase` for variables, `PascalCase` for classes)
- Formatting/linting tools (e.g., Prettier, ESLint, Black, gofmt)

## Testing Guidelines
No testing framework is configured yet. When you add tests, include:
- The framework used (e.g., Jest, Pytest, Go test)
- Test naming patterns (e.g., `*.spec.ts`, `test_*.py`)
- Coverage expectations, if any

## Commit & Pull Request Guidelines
No commit history exists yet, so no conventions can be inferred. Until defined, use a clear, imperative subject line, for example:
- `Add baseline project structure`
- `Introduce action detection prototype`

For pull requests, include:
- A brief description of the change
- Testing notes (commands run)
- Screenshots or logs when UI or behavior changes

## Security & Configuration Tips
If the project uses secrets, never commit them. Store local configuration in files like `.env` and document required keys in `README.md` or `docs/`.
