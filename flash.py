#!/usr/bin/python2

from pynrfjprog import API, Hex
import sys


def flash(hex_file_path):
    api = API.API('NRF51')
    api.open()
    serial_numbers = api.enum_emu_snr()

    for snr in serial_numbers:
        api.connect_to_emu_with_snr(snr)
        assert api.is_connected_to_emu()

        # api.connect_to_device()?
        api.erase_all()

        program = Hex.Hex(hex_file_path)
        print('# Flashing on {}'.format(snr))
        for segment in program:
            api.write(segment.address, segment.data, True)

        api.sys_reset()
        print('# Device reset... ')

        api.go()
        print('# Application running...  ')

        api.disconnect_from_emu()
        print('')

    api.close()

if __name__ == '__main__':
    assert len(sys.argv) == 2
    hex_file_path = "./Output/Debug/Exe/" + sys.argv[1] + ".hex"
    print("# Flashing %s" % hex_file_path)

    flash(hex_file_path)
    #  if len(sys.argv) > 1:
        #  hex_file_path = sys.argv[1]
        #  flash(hex_file_path)
    #  else:
        #  print("# Please provide hex file path")
        #  exit()
