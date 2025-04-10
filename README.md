# Narwhal - NFS Read/Write Locks

Provides (hopefully robust) read/write locks between distributed processes using shared NFS storage. This is just a
single `.h` file and a single `.c` file you can just drop into your project. It is implemented in C99. See the header
file for the API and detailed documentation. Usage is "simple":

```c
#include <narwhal.h>

narwhal = Narwhal {
        .lockdir = "/some/path/to/lockdir",
        .spin_usec = 1000,
        .timeout_sec = 10
};

narwhal_read_lock(&narwhal)
read_protected_data()
narwhal_unlock(&narwhal)

narwhal_write_lock(&narwhal)
read_protected_data()
write_updated_data()
narwhal_unlock(&narwhal)
```

## License (MIT)

Copyright © 2025 Weizmann Institute of Science

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
