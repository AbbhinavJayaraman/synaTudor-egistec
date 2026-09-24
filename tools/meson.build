# Test tools. None of these need the sensor, and none are installed.
#
# bringup_nosensor drives the real tudor_init()/tudor_open() against a simulated
# EH575, which is what makes DLL bring-up debuggable without the hardware - and
# under gdb, which sudo otherwise makes awkward.
executable('bringup_nosensor', 'bringup_nosensor.c',
    dependencies: [libtudor_dep, libcrypto_dep, libusb_dep],
    c_args: ['-D_GNU_SOURCE'],
    install: false
)

# Shim unit tests. Both call into libtudor through the Microsoft vararg
# convention on purpose - see the comments in the sources.
foreach t : ['test_format', 'test_convert']
    exe = executable(t, t + '.c',
        dependencies: [libtudor_dep],
        c_args: ['-D_GNU_SOURCE'],
        install: false
    )
    test(t, exe)
endforeach
