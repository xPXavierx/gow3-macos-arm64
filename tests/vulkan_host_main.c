#include <stdio.h>
int vulkan_smoke(void);
int main(void) {
    puts("Host Vulkan submission test only; does not validate the game renderer.");
    return vulkan_smoke();
}
