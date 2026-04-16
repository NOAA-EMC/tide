# Contributing

Thank you for your interest in contributing to TIDE! This guide covers the
contribution workflow, how to build and preview the documentation locally, and
how to add new documentation pages.

## Contribution Workflow

1. **Fork** the repository on GitHub.
2. **Clone** your fork and create a feature branch:

    ```bash
    git clone https://github.com/<your-user>/TIDE.git
    cd TIDE
    git checkout -b my-feature
    ```

3. **Make your changes** — code, documentation, or both.
4. **Commit** with a clear, descriptive message:

    ```bash
    git add .
    git commit -m "Add description of change"
    ```

5. **Push** your branch and open a **Pull Request** against the `main` branch:

    ```bash
    git push origin my-feature
    ```

6. The CI pipeline will automatically build the documentation and run tests on
   your PR. Address any failures before requesting review.

## Local Documentation Build and Preview

The documentation site is built with [Zensical](https://zensical.org/) and uses
the [MkDoxy](https://mkdoxy.kubaandrysek.cz/) plugin to generate API reference
pages from Doxygen comments in the source code.

### Prerequisites

- **Python 3.10+**
- **Doxygen** — required by MkDoxy to parse Fortran and C++ source files. If
  Doxygen is not installed, the MkDoxy plugin will report an error during the
  build. Install it with your system package manager:

    ```bash
    # Ubuntu / Debian
    sudo apt-get install -y doxygen

    # macOS (Homebrew)
    brew install doxygen

    # Fedora / RHEL
    sudo dnf install doxygen
    ```

### Install Python Dependencies

From the repository root, install the documentation toolchain:

```bash
pip install -r docs/requirements.txt
```

This installs `zensical` and `mkdoxy`.

### Build and Serve Locally

Start a local development server with live reload:

```bash
zensical serve
```

The site will be available at `http://127.0.0.1:8000`. Changes to Markdown files
are picked up automatically and the browser will refresh.

To produce a static build without serving:

```bash
zensical build
```

The output is written to the `site/` directory.

## Adding New Documentation Pages

1. Create a new Markdown file under the appropriate `docs/` subdirectory. For
   example, to add a troubleshooting page to the User Guide:

    ```bash
    docs/user-guide/troubleshooting.md
    ```

2. Add a heading and content to the file:

    ```markdown
    # Troubleshooting

    Common issues and solutions.
    ```

3. Register the page in `mkdocs.yml` by adding it to the `nav` section under
   the correct heading:

    ```yaml
    nav:
      - User Guide:
          - Troubleshooting: user-guide/troubleshooting.md
    ```

4. Preview your changes with `zensical serve` and verify the new page appears
   in the navigation.

### Adding API Reference Pages

API reference pages use MkDoxy snippet directives to embed auto-generated
documentation from Doxygen comments. To document a Fortran module, add a
directive like:

```markdown
::: doxy.tide.Class
    name: my_module_name
```

The project name (`tide`) must match the project defined in `mkdocs.yml` under
`plugins.mkdoxy.projects`. Doxygen must be installed locally for these
directives to render during preview.
