---
name: zip-backup
description: Create a zip backup of the ParamotorSim project source and assets.
allowed-tools:
  - exec
permissions:
  allow:
    - Exec(C:\Program Files\7-Zip\7z.exe)
    - Exec(python)
---

Create a timestamped zip backup of the ParamotorSim project.

1. Use 7-Zip to archive the project directory, excluding `Binaries`, `Intermediate`, `Saved`, and `.git`.
2. Save the archive to `C:\DevinTools\backups\ParamotorSim_YYYY-MM-DD_HH-MM.zip`.
3. Create the backup directory if it does not exist.
4. Report the archive path and size.
