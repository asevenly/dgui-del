cmake_minimum_required( VERSION 3.7...3.27 )
project( dgui VERSION 0.0.0 LANGUAGES C )

set( A7_LIB a7__${PROJECT_NAME} )
add_subdirectory( source )
add_library( a7::${PROJECT_NAME} ALIAS ${A7_LIB} )
target_include_directories( ${A7_LIB} PUBLIC include )

# Apache License 2.0
# NO WARRANTY OF ANY KIND see <http://www.apache.org/licenses/LICENSE-2.0>
# SPDX-License-Identifier: Apache-2.0
# lib: [`NAME`] [LEGEND]
# Ⓒ Copyright (c) 2026 Oleg'Ease'Kharchuk ᦒ
