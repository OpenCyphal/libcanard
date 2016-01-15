#include <stdio.h>
#include <stdint.h>
/**
 * CRC-16-CCITT
 * Initial value: 0xFFFF
 * Poly: 0x1021
 * Reverse: no
 * Output xor: 0
 *
 * import crcmod
 * crc = crcmod.predefined.Crc('crc-ccitt-false')
 * crc.update('123456789')
 * crc.hexdigest()
 * '29B1'
 */

uint16_t crc_add_byte(uint16_t crc_running_val, uint8_t byte)
{
    crc_running_val ^= (uint16_t)((uint16_t)(byte) << 8);
    int j;
    for (j=0; j<8; j++)
    {
        if (crc_running_val & 0x8000U)
        {
            crc_running_val = (uint16_t)((uint16_t)(crc_running_val << 1) ^ 0x1021U);
        }
        else
        {
            crc_running_val = (uint16_t)(crc_running_val << 1);
        }
    }
    return crc_running_val;
}

uint16_t crc_add(uint16_t crc_running_val, const uint8_t* bytes, uint16_t len)
{
    while (len--)
    {
        crc_running_val = crc_add_byte(crc_running_val,*bytes++);
    }
    return crc_running_val;
}

uint16_t crc_reset(void)
{

    return 0XFFFFU;
}


int main(int argc, char** argv)
{
    uint16_t crc_running_val = crc_reset();
    crc_running_val = crc_add_byte(crc_running_val,'1');
    crc_running_val = crc_add_byte(crc_running_val,'2');
    crc_running_val = crc_add_byte(crc_running_val,'3');
    printf("%X \n", crc_running_val);

    crc_running_val = crc_add(crc_running_val, "456789", 6);
    printf("%X \n", crc_running_val);
    crc_running_val = crc_reset();
}