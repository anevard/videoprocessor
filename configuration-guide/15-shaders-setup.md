# 15. Shaders — Setup

![Shader Setup screen](images/15-shaders-setup.png)

- **Shader cache status** — Shows whether the persistent VP Renderer shader cache exists, its size, and its last update time.
- **Clear shader cache** — Requests removal of the persistent cache. VP recompiles required shaders later.

Clear the cache after a shader/rendering upgrade or when diagnosing a suspected stale-cache problem. The next run may spend longer compiling shaders.

---

[← Configuration guide index](README.md)
