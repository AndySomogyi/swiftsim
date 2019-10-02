#!/usr/bin/env python3
"""
Read a logger file by using an index file.
Example: ./reader_example.py ../../examples/SedovBlast_3D/index 0.1
"""
import sys
import numpy as np
import matplotlib.pyplot as plt
sys.path.append("../.libs/")

import liblogger as logger

if len(sys.argv) >= 2:
    basename = sys.argv[1]
else:
    print("No basename supplied (first argument), using default.")
if len(sys.argv) >= 3:
    time = sys.argv[2]
else:
    print("No time supplied (second argument), using default.")
if len(sys.argv) > 3:
    print("Ignoring excess arguments '%s'." % sys.argv[3:])
print("basename: %s" % basename)
print("time: %g" % time)

# read dump
data = logger.loadFromIndex(basename, time)

pos = data["positions"]


def plot3D():
    from mpl_toolkits.mplot3d import Axes3D
    fig = plt.figure()
    ax = fig.add_subplot(111, projection="3d")
    ax.plot(data["positions_x"], data["positions_y"],
            data["positions_z"], ".")


def plot2D():
    center = np.array([0.5]*3)
    r2 = np.sum((pos - center)**2, axis=1)

    # plot entropy vs distance
    plt.plot(np.sqrt(r2), data["entropies"], '.',
             markersize=0.2)

    plt.xlim(0., 0.5)
    plt.ylim(-1, 50)
    plt.xlabel("Radius")
    plt.ylabel("Entropy")


plot2D()
plt.show()