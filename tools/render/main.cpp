// fss_render: offline renderer (host).
//
// L0.3: only proves that a host builds and links against fss_core. From L1.1 on, it will turn a
// note list + patch into a .wav in renders/. Hosts are the only code allowed to do I/O (CLAUDE.md).

#include <cstdio>

int main()
{
    std::puts("hello from fss_render (CFS L0.3 skeleton)");
    return 0;
}
