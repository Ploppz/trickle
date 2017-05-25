#!/usr/bin/python2

from pynrfjprog import API, Hex
import sys


def reset():
    api = API.API('NRF51')
    api.open()
    serial_numbers = api.enum_emu_snr()

    for snr in serial_numbers:
        api.connect_to_emu_with_snr(snr)
        assert api.is_connected_to_emu()


        api.sys_reset()
        print('# Device reset... ')

        api.go()
        print('# Application running...  ')

        api.disconnect_from_emu()
        print('')

    api.close()

if __name__ == '__main__':
    reset()
