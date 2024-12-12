# PAlib

Welcome to PAlib 100707!

PAlib is a Nintendo DS library used to create homebrew games. It is designed to
be easily used by anyone. However, nowadays there are better alternatives and
PAlib should only be used to build old projects that depend on it to be built.

To install PAlib you'll need to install BlocksDS first. Then, clone this
repository, enter that directory and run:

```sh
make install
```

Directory structure:

Templates:
- `template`: Contains a PAlib project template.

Resources:
- `examples`: Contains examples that show you how to use PAlib.
- `docs`: Contains the documentation in two languages (English and French).
- `tools`: Contains tools that will aid you in the development process of your projects.

Library source folders:
- `include`: Contains the library headers.
- `source`: Contains the PAlib source code.
- `sys`: Contains the source code of each PAlib ARM7 binary.