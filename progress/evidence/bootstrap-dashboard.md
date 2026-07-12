# Bootstrap dashboard build

- Evidence ID: `bootstrap.dashboard`
- Build command: `python tools/progress/build.py --evidence progress/progress.json --out progress/site --markdown PROGRESS.md`
- Determinism check: `python tools/progress/build.py --evidence progress/progress.json --out progress/site --markdown PROGRESS.md --check`
- Result: generated outputs current
- Outputs: `progress/site/index.html`, `progress/site/progress.json`, and `PROGRESS.md`
- Nintendo-derived data: none
