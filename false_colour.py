#!/usr/bin/python

from math import sin, cos, pi

POINTS = 16


def colour_cycle(points, fn=cos):
    colours = []
    for i in range(points):
        a = i * 2.0 * pi / points
        _r = fn(a)
        _g = fn(a + pi * 2 / 3.0)
        _b = fn(a + pi * 4 / 3.0)

        r = (int(255.99 * (_r + 1) * 0.5))
        g = (int(255.99 * (_g + 1) * 0.5))
        b = (int(255.99 * (_b + 1) * 0.5))

        colours.append((r, g, b))
    return colours

def hex_format(colours):
    return ["%02x%02x%02x" % x for x in colours]

def html_test(colours):
    print "<html><body>"
    for c in colours:
        rgb = "#%02x%02x%02x" % c
        print '<div style="width:100%%; height:30px; background: %s">' % (rgb, )
        print rgb
        print '</div>'
    print '</body></html>'

def cformat(colours, name, wrap=75, unused=True):
    # convert to 32bit mask by replicating first couplet as last
    # the mask will work whether the format is RGB_, _RGB, BGR_, or _BGR
    table = ['0x%02x%02x%02x%02x' % (c[2], c[0], c[1], c[2]) for c in colours]
    table.append('0x000000')
    outs = []
    if unused:
        outs.append('UNUSED')

    outs.extend(("static const guint32 %s [%s] = {" % (name, len(table)),
                 '    %s' % table[0]))
    for v in table[1:]:
        if len(outs[-1]) >= wrap:
            outs[-1] += ','
            outs.append('    %s' % v)
        else:
            outs[-1] += ', %s' % v
    outs.append('};\n')

    print '\n'.join(outs)



c = colour_cycle(POINTS)
#print hex_format(c)
#html_test(c)
cformat(c, "lag_false_colour")
