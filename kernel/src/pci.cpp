#include <stdint.h>

uint32_t inl(uint16_t port) {
    uint32_t result;
    asm volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void outl(uint16_t port, uint32_t value) {
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t reg) {
    uint32_t address = (1 << 31 );
    bus << 16;
    device << 11;
    function << 8;
    reg << 2;
}