# Git history provenance

The original local `.git` directory supplied for this project had an unborn `main`
branch and contained no commits. Therefore, the earlier development history could
not be recovered as original commit-by-commit history.

To establish a truthful release history, the following commits and annotated tags
were reconstructed from the exact source release archives. Each commit represents
one complete release snapshot; these are **not** the original feature-development
commits and their Git timestamps must not be interpreted as original release dates.

| Tag | Source archive | Verified SHA-256 |
|---|---|---|
| `v1.6.1` | `FeinStatic-1.6.1-source.tar.gz` | `1b50c65e30d108b821c44c340aa230338f8ff24f09840f862a4e392bc56b6abe` |
| `v1.7.0` | `FeinStatic-1.7.0-source.tar.gz` | `de4a94b23328ffae9c18faba6ef64696b1928bbf8ebfc79f100c71e50750e432` |
| `v1.8.0` | `FeinStatic-1.8.0-source.tar.gz` | `6a2c1bd6a3bd521d0a41724f2d4595c4ba498b1ecdae80d8bcadf60784ae74cc` |
| `v1.9.0` | `FeinStatic-1.9.0-source.tar.gz` | `148fa5fd53682bc650965c6375a7568fa901786cc87e99c791d23d5cbf433744` |

The archive hashes were checked before constructing this history. Runtime databases,
node identities, build directories and local backups are intentionally excluded
from the Git repository.
