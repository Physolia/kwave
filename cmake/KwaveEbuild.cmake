#############################################################################
##    Kwave                - cmake/KwaveEbuild.cmake
##                           -------------------
##    begin                : Wed May 23 2007
##    copyright            : (C) 2007 by Thomas Eschenbacher
##    email                : Thomas.Eschenbacher@gmx.de
#############################################################################
#
#############################################################################
##                                                                          #
##    This program is free software; you can redistribute it and/or modify  #
##    it under the terms of the GNU General Public License as published by  #
##    the Free Software Foundation; either version 2 of the License, or     #
##    (at your option) any later version.                                   #
##                                                                          #
#############################################################################

SET(_ebuild ${CMAKE_CURRENT_BINARY_DIR}/kwave-${KWAVE_VERSION_FULL}.ebuild)

ADD_CUSTOM_COMMAND(OUTPUT ${_ebuild}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/kwave.ebuild ${_ebuild}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/kwave.ebuild
)

ADD_CUSTOM_TARGET(ebuild ALL
    DEPENDS ${_ebuild}
)

#############################################################################
#############################################################################
