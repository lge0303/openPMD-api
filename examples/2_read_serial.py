#!/usr/bin/env python
"""
This file is part of the openPMD-api.

Copyright 2018 openPMD contributors
Authors: Axel Huebl
License: LGPLv3+
"""
import openPMD


if __name__ == "__main__":
    series = openPMD.Series("../samples/git-sample/data%T.h5",
                            openPMD.Access_Type.read_only)
    print("Read a Series with openPMD standard version %s" %
          series.openPMD)

    print("The Series contains {0} iterations:".format(len(series.iterations)))
    for i in series.iterations:
        print("\t {0}".format(i))
    print("")

    i = series.iterations[100]
    print("Iteration 100 contains {0} meshes:".format(len(i.meshes)))
    for m in i.meshes:
        print("\t {0}".format(m))
    print("")
    print("Iteration 100 contains {0} particle species:".format(
        len(i.particles)))
    for ps in i.particles:
        print("\t {0}".format(ps))
    print("")

    E_x = i.meshes["E"]["x"]
    shape = E_x.shape

    print("Field E.x has shape {0} and datatype {1}".format(
          shape, E_x.dtype))

    offset = [1, 1, 1]
    extent = [2, 2, 1]
    # TODO buffer protocol / numpy bindings
    # chunk_data = E_x[1:3, 1:3, 1:2]
    chunk_data = E_x.load_chunk(offset, extent)
    # print("Queued the loading of a single chunk from disk, "
    #       "ready to execute")
    series.flush()
    print("Chunk has been read from disk\n"
          "Read chunk contains:")
    print(chunk_data)
    # for row in range(2):
    #     for col in range(2):
    #         print("\t({0}|{1}|{2})\t{3}".format(
    #            row + 1, col + 1, 1, chunk_data[row*chunk_extent[1]+col])
    #         )
    #     print("")
