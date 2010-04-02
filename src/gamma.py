#!/usr/bin/python


def gamma_table(start=0, stop=256, gamma=0.45):
    lut = []
    spread = stop - start
    for point in range(spread):
        p1 = float(point) / spread
        p1 = p1 ** gamma
        out = int(p1 * spread) + start
        if out >= stop:
            out = stop - 1
        lut.append(out)

    if start > 0:
        lut[0:0] = [lut[0]] * start
    if stop < 256:
        lut.extend([lut[-1]] * (256 - stop))

    return lut


def srgb_to_linear_table():
    lut = []
    a = 0.055
    for point in range(256):
        p = point / 255.0
        if p <= 0.4545:
            p /= 12.92
        else:
            p = ((p + a) / (1 + a)) ** 2.4
        lut.append(int(p * 255))
    return lut

def linear_to_srgb_table():
    lut = []
    a = 0.055
    for point in range(256):
        p = point / 255.0
        if p <= 0.0031308:
            p *= 12.92
        else:
            p = (1 + a) * (p ** (1 / 2.4)) - a
        lut.append(int(p * 255))
    return lut


def cformat(table, name="SOME_TABLE", wrap=75):
    table = list(table)
    outs = ["static guint8 %s [%s] = {" % (name, len(table)),
            '    %s' % table[0]]
    for v in table[1:]:
        if len(outs[-1]) >= wrap:
            outs[-1] += ','
            outs.append('    %s' % v)
        else:
            outs[-1] += ', %s' % v
    outs.append('};\n')

    print '\n'.join(outs)


print "/* Gamma corrected lookup tables for inverting video */"
print "/* generated by '%s'. DO NOT EDIT */" % __file__
cformat(reversed(gamma_table(gamma=2.0)), "sparrow_rgb_gamma_full_range_REVERSE")
cformat(reversed(gamma_table(16, 236)), "sparrow_rgb_gamma_headroom_REVERSE")



