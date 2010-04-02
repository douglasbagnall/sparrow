#!/usr/bin/python


def gamma_table(table, gamma=0.45):
    for point in table:
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
    


def input_table(start=0, stop=256):
    lut = [0] * start
    spread = stop - start    
    lut.extend(range(spread))
    lut.extend([spread - 1] * (256 - stop))
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


inputs_full = input_table()
inputs_headroom = input_table(16, 236)

cformat(reversed(gamma_table(inputs_full)), "sparrow_rgb_gamma_full_range_REVERSE")
cformat(reversed(gamma_table(inputs_headroom)), "sparrow_rgb_gamma_headroom_REVERSE")

cformat(reversed(gamma_table(inputs_full)), "sparrow_rgb_gamma_full_range_REVERSE")
cformat(reversed(gamma_table(inputs_headroom)), "sparrow_rgb_gamma_headroom_REVERSE")


