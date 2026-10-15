#!/usr/bin/env python3
import numpy as np
import scipy.signal as signal

def printDesign(sos, fs : int):
    bsos = 'std::vector< std::array<double, 3> > bs{}\n'.format(fs)
    bsos = bsos + '{\n'
    asos = 'std::vector< std::array<double, 3> > as{}\n'.format(fs)
    asos = asos + '{\n'
    for s in range(len(sos)):
        row = sos[s]
        bsos = bsos + '     std::array<double, 3> {' + ' {}, {}, {} '.format(row[0], row[1], row[2]) + ' }'
        asos = asos + '     std::array<double, 3> {' + ' {}, {}, {} '.format(row[3], row[4], row[5]) + ' }'
        if (s < len(sos) - 1): 
            bsos = bsos + ',\n'
            asos = asos + ',\n'
        else:
            bsos = bsos + '\n'
            asos = asos + '\n'

    bsos = bsos + '};\n'
    asos = asos + '};\n'
    print(bsos)
    print(asos) 
    
def designBandpass(N = 3, fs : int = 100):
    if (fs == 40):
        Wn = [0.5, 18.0]
    elif (fs == 80):
        Wn = [0.5, 38.0] 
    elif (fs == 100):
        Wn = [0.5, 45.0]
    elif (fs == 200):
        Wn = [0.5, 90.0]
    elif (fs == 250):
        Wn = [0.5, 115.0]
    elif (fs == 500):
        Wn = [0.5, 230.0] 
    elif (fs == 1000):
        Wn = [0.5, 400.0]
    else:
        raise Exception("unhandled fs")

    sos = signal.iirfilter(N,
                           Wn,
                           analog = False,
                           ftype = 'butter',
                           btype = 'bandpass',
                           fs = fs,
                           output = 'sos')
    printDesign(sos, fs)
    return sos
         

def design100HzBandpass(N = 3):
    return designBandpass(N, 100)

def design40HzBandpass(N = 3): 
    return designBandpass(N, 40)

def design80HzBandpass(N = 3): 
    return designBandpass(N, 80) 

def design200HzBandpass(N = 3):
    return designBandpass(N, 200)

def design250HzBandpass(N = 3):
    return designBandpass(N, 250)

def design500HzBandpass(N = 3):
    return designBandpass(N, 500)

def design1000HzBandpass(N = 3):
    return designBandpass(N, 1000)

def impulse_response(sos, fs : int = 100, N = 1000):
    delta = np.zeros(N)
    delta[0] = 1
    y = signal.sosfilt(sos, delta)
    fname = 'bandpassReferenceFS-{}.txt'.format(fs)
    ofl = open(fname, 'w')
    for i in range(len(y)):
        ofl.write("{}\n".format(y[i]))
    ofl.close()
    #print(y)


if __name__ == "__main__":
    sos40 = design40HzBandpass()
    sos80 =  design80HzBandpass()
    sos100 = design100HzBandpass()
    sos200 = design200HzBandpass()
    sos500 = design500HzBandpass()
    sos1000 = design1000HzBandpass()

    impulse_response(sos40, 40)
    impulse_response(sos80, 80)
    impulse_response(sos100, 100)
    impulse_response(sos200, 200)
    impulse_response(sos500, 500)
    impulse_response(sos1000, 1000)
    #sos = design100HzBandpass()
    #impulse_response(sos)
