#!/usr/bin/env python3
"""GO CODE - mission floor plans. Builds each map from rooms / walls / doors / props and writes
source/maps.inc (the ASCII maps the game reads).  python3 tools/maps.py [show]

Legend:
  outdoors   .  grass        ,  pavement
  walls      #  wall         %  cracked wall (breachable)   W  window
  doors      D  door         R  reinforced door (breach only)
  floors     w  wood  c  carpet  t  tile  k  concrete  m  marble  d  dance floor
  props      T table  S sofa  K desk  B bed  X crate  C counter  P plant  H shelf (blocks sight)  V car
  lights     l lamp  f fluorescent  n neon  L street light
  actors     e thug  g gunman  h heavy  G hostage guard  @ hostage  s squad start
"""
import sys, os

W, H = 34, 17

class Map:
    def __init__(self, out='.'):
        self.g = [[out] * W for _ in range(H)]
    def fill(self, x0, y0, x1, y1, ch):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.g[y][x] = ch
    def room(self, x0, y0, x1, y1, floor, wall='#'):
        """walls on the border (inclusive), floor inside"""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                edge = x in (x0, x1) or y in (y0, y1)
                if edge:
                    if self.g[y][x] not in 'DRW%':
                        self.g[y][x] = wall
                else:
                    self.g[y][x] = floor
    def hwall(self, x0, x1, y, ch='#'):
        for x in range(x0, x1 + 1):
            self.g[y][x] = ch
    def vwall(self, x, y0, y1, ch='#'):
        for y in range(y0, y1 + 1):
            self.g[y][x] = ch
    def put(self, x, y, s):
        for i, ch in enumerate(s):
            self.g[y][x + i] = ch
    def at(self, pts, ch):
        for (x, y) in pts:
            self.g[y][x] = ch
    def rows(self):
        return [''.join(r) for r in self.g]

def check(name, rows):
    errs = []
    n = {c: sum(r.count(c) for r in rows) for c in 'sG@egh'}
    if n['s'] != 4: errs.append('needs 4 starts, has %d' % n['s'])
    if n['G'] != n['@']: errs.append('guards %d != hostages %d' % (n['G'], n['@']))
    # doors must sit between two walls
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c in 'DR':
                horiz = 0 < x < W - 1 and r[x - 1] in '#%WDR' and r[x + 1] in '#%WDR'
                vert = 0 < y < H - 1 and rows[y - 1][x] in '#%WDR' and rows[y + 1][x] in '#%WDR'
                if not (horiz or vert):
                    errs.append('door at %d,%d not between walls' % (x, y))
    for e in errs:
        print('  %s: %s' % (name, e))
    return not errs

MAPS = []

# ---------------------------------------------------------------- 1 SAFEHOUSE
m = Map('.')
m.fill(0, 14, 33, 16, ',')                      # street
m.fill(16, 12, 18, 13, ',')                     # front path
m.room(5, 2, 28, 11, 'w')                       # house shell
m.hwall(5, 28, 6)                               # north rooms / living room
m.vwall(13, 2, 6); m.vwall(21, 2, 6)
m.fill(6, 3, 12, 5, 't')                        # kitchen
m.fill(14, 3, 20, 5, 'c')                       # bedroom
m.fill(22, 3, 27, 5, 'w')                       # study
m.at([(9, 6), (17, 6), (24, 6)], 'D')           # inner doors
m.at([(17, 11)], 'D')                           # front door
m.at([(5, 4)], 'D')                             # kitchen side door
m.at([(13, 4)], '%')                            # cracked wall kitchen | bedroom
m.at([(9, 2), (17, 2), (25, 2), (11, 11), (23, 11), (28, 8)], 'W')
m.put(7, 3, 'CC'); m.put(9, 4, 'f'); m.put(7, 5, 'TT')
m.put(15, 3, 'BB'); m.put(19, 3, '@'); m.put(16, 4, 'G'); m.put(19, 5, 'l')
m.put(23, 3, 'K'); m.put(25, 4, 'l'); m.put(27, 5, 'e')
m.put(7, 8, 'SSS'); m.put(12, 8, 'l'); m.put(22, 8, 'TT'); m.put(26, 9, 'e'); m.put(8, 10, 'e'); m.put(9, 10, 'P'); m.put(25, 10, 'P')
m.put(20, 9, 'l')
m.at([(2, 13), (31, 13)], 'L'); m.put(3, 1, 'P'); m.put(30, 2, 'P')
m.at([(13, 15), (14, 15), (20, 15), (21, 15)], 's')
MAPS.append(('SAFEHOUSE', m.rows()))

# ---------------------------------------------------------------- 2 CORNER STORE
m = Map('.')
m.fill(0, 0, 33, 1, ',')                        # back alley
m.fill(0, 15, 33, 16, ',')                      # street
m.fill(0, 2, 2, 14, ',')                        # side lot
m.room(3, 2, 30, 13, 't')
m.hwall(3, 30, 7)
m.vwall(14, 2, 7)
m.fill(4, 3, 13, 6, 'k')                        # storage
m.fill(15, 3, 29, 6, 'c')                       # back office
m.at([(9, 2)], 'R')                             # reinforced back door (storage)
m.at([(25, 2)], 'D')                            # office back door
m.at([(8, 7), (18, 7)], 'D')
m.at([(14, 5)], '%')
m.at([(16, 13)], 'D'); m.at([(3, 10)], 'D')     # front door, side door
m.at([(7, 13), (8, 13), (11, 13), (12, 13), (20, 13), (21, 13), (24, 13), (25, 13)], 'W')
m.put(5, 3, 'XX'); m.put(12, 3, 'X'); m.put(5, 6, 'X'); m.put(12, 6, 'XX')
m.put(8, 4, 'f'); m.put(10, 5, 'e')
m.put(16, 3, 'KK'); m.put(22, 3, '@'); m.put(20, 4, 'G'); m.put(27, 4, 'l'); m.put(28, 6, 'P'); m.put(17, 5, 'g')
m.put(5, 9, 'HHHH'); m.put(5, 11, 'HHHH'); m.put(11, 9, 'HHH'); m.put(11, 11, 'HHH')
m.put(21, 9, 'CCCC'); m.put(16, 10, 'f'); m.put(9, 10, 'f'); m.put(26, 10, 'f')
m.put(24, 11, 'e'); m.put(13, 12, 'e')
m.at([(4, 0), (29, 16)], 'L'); m.at([(6, 1), (27, 1)], 'X')
m.at([(13, 16), (14, 16), (18, 16), (19, 16)], 's')
MAPS.append(('CORNER STORE', m.rows()))

# ---------------------------------------------------------------- 3 MOTEL 9
m = Map('.')
m.fill(0, 12, 33, 16, ',')                      # parking lot
m.fill(0, 10, 33, 11, ',')                      # walkway
for i, x0 in enumerate([1, 9, 17, 25]):         # four rooms in a row
    m.room(x0, 2, x0 + 8, 9, 'c')
    m.hwall(x0, x0 + 8, 6)                      # bathroom at the back
    m.fill(x0 + 1, 3, x0 + 7, 5, 't')
    m.at([(x0 + 3, 6)], 'D')
    m.at([(x0 + 6, 9)], 'D')
    m.at([(x0 + 3, 9)], 'W')
    m.put(x0 + 1, 7, 'BB')
    m.put(x0 + 7, 7, 'l')
m.at([(9, 7), (17, 4), (25, 7)], '%')           # thin walls between some rooms
m.put(2, 4, 'f'); m.put(5, 8, 'e'); m.put(7, 4, 'K')
m.put(13, 8, '@'); m.put(12, 7, 'G'); m.put(14, 4, 'g')
m.put(20, 8, 'h'); m.put(22, 4, 'T'); m.put(18, 4, 'f')
m.put(30, 8, '@'); m.put(29, 7, 'G'); m.put(32, 4, 'e'); m.put(26, 4, 'f')
m.put(3, 13, 'VV'); m.put(11, 13, 'VV'); m.put(23, 14, 'VV')
m.at([(0, 11), (16, 11), (33, 11)], 'L'); m.at([(31, 13), (17, 13)], 'L')
m.at([(14, 15), (15, 15), (19, 15), (20, 15)], 's')
MAPS.append(('MOTEL 9', m.rows()))

# ---------------------------------------------------------------- 4 OFFICE FLOOR
m = Map('.')
m.fill(0, 14, 33, 16, ',')
m.room(2, 1, 31, 13, 'c')
m.vwall(12, 1, 13); m.vwall(21, 1, 13)
m.hwall(2, 12, 6); m.hwall(21, 31, 6)
m.fill(3, 2, 11, 5, 'c')                        # boss office
m.fill(3, 7, 11, 12, 't')                       # break room
m.fill(13, 2, 20, 12, 'c')                      # open plan
m.fill(22, 2, 30, 5, 'w')                       # meeting room
m.fill(22, 7, 30, 12, 'k')                      # server room
m.at([(12, 4), (12, 9), (21, 3), (21, 10)], 'D')
m.at([(7, 6)], '%'); m.at([(26, 6)], 'R')
m.at([(16, 13)], 'D'); m.at([(2, 10)], 'D')
m.at([(13, 6), (13, 7), (20, 6), (20, 7)], 'W')  # glass partitions: interior windows? (kept as walls below)
m.at([(13, 6), (13, 7), (20, 6), (20, 7)], 'c')
m.at([(5, 1), (8, 1), (16, 1), (25, 1), (28, 1), (31, 3), (31, 9), (2, 3)], 'W')
m.put(5, 3, 'KK'); m.put(9, 2, 'P'); m.put(6, 4, '@'); m.put(8, 4, 'G'); m.put(10, 5, 'l')
m.put(5, 8, 'TT'); m.put(9, 11, 'CC'); m.put(4, 11, 'f'); m.put(8, 9, 'e')
for y in (3, 6, 9):
    m.put(14, y, 'KK'); m.put(18, y, 'KK')
m.put(16, 4, 'f'); m.put(16, 10, 'f'); m.put(17, 7, 'g'); m.put(15, 11, 'e')
m.put(24, 3, 'TTTT'); m.put(23, 5, 'l'); m.put(29, 2, 'P'); m.put(28, 4, 'g')
m.put(23, 8, 'HH'); m.put(23, 11, 'HH'); m.put(28, 8, 'HH'); m.put(28, 11, 'HH'); m.put(26, 9, 'f'); m.put(26, 11, '@'); m.put(25, 10, 'G')
m.at([(0, 14), (33, 14)], 'L')
m.at([(14, 15), (15, 15), (17, 15), (18, 15)], 's')
MAPS.append(('OFFICE FLOOR', m.rows()))

# ---------------------------------------------------------------- 5 WAREHOUSE
m = Map(',')
m.room(2, 1, 31, 13, 'k')
m.room(2, 1, 9, 6, 'k'); m.room(24, 1, 31, 6, 'w')
m.fill(3, 2, 8, 5, 'k'); m.fill(25, 2, 30, 5, 'w')
m.at([(9, 4), (24, 4)], 'D')
m.at([(16, 13), (17, 13)], 'R')                 # loading doors
m.at([(2, 10), (31, 10)], 'D')
m.at([(6, 6)], '%')
m.at([(12, 1), (16, 1), (20, 1)], 'W')
for (x, y, s) in [(11, 3, 'XX'), (11, 4, 'XX'), (19, 3, 'XXX'), (12, 7, 'HHHH'), (19, 7, 'HHHH'), (12, 10, 'HH'),
                  (20, 10, 'HH'), (4, 8, 'XX'), (4, 9, 'X'), (28, 8, 'X'), (27, 9, 'XX'), (15, 4, 'X')]:
    m.put(x, y, s)
m.put(5, 3, '@'); m.put(7, 3, 'G'); m.put(4, 5, 'f')
m.put(28, 3, '@'); m.put(26, 2, 'K'); m.put(29, 4, 'G'); m.put(26, 5, 'l')
m.put(10, 6, 'f'); m.put(17, 6, 'f'); m.put(23, 6, 'f'); m.put(10, 11, 'f'); m.put(23, 11, 'f')
m.put(16, 9, 'h'); m.put(13, 5, 'e'); m.put(22, 4, 'g'); m.put(7, 11, 'e'); m.put(26, 11, 'g'); m.put(17, 11, 'e')
m.put(4, 15, 'VVV'); m.put(26, 15, 'VVV'); m.at([(1, 14), (32, 14), (12, 15), (21, 15)], 'L')
m.at([(15, 15), (16, 15), (17, 15), (18, 15)], 's')
MAPS.append(('WAREHOUSE 17', m.rows()))

# ---------------------------------------------------------------- 6 NIGHTCLUB
m = Map(',')
m.room(2, 1, 31, 13, 'w')
m.vwall(10, 1, 13); m.vwall(23, 1, 13)
m.hwall(2, 10, 7); m.hwall(23, 31, 7)
m.fill(11, 2, 22, 12, 'w'); m.fill(13, 4, 20, 9, 'd')     # main floor with the dance floor
m.fill(3, 2, 9, 6, 't')                         # restrooms
m.fill(3, 8, 9, 12, 'k')                        # back room
m.fill(24, 2, 30, 6, 'c')                       # VIP
m.fill(24, 8, 30, 12, 'c')                      # office
m.at([(10, 4), (10, 10), (23, 4), (23, 10), (27, 7)], 'D')
m.at([(16, 13), (17, 13)], 'D'); m.at([(2, 10)], 'R'); m.at([(31, 4)], 'D')
m.at([(6, 7)], '%')
m.put(11, 2, 'CCCCC'); m.put(12, 12, 'TT'); m.put(20, 12, 'TT'); m.put(21, 2, 'n'); m.put(11, 11, 'n'); m.put(22, 11, 'n'); m.put(16, 2, 'n')
m.put(14, 6, 'e'); m.put(19, 8, 'g'); m.put(12, 7, 'e')
m.put(4, 3, 'f'); m.put(7, 5, 'e')
m.put(4, 9, 'XX'); m.put(8, 11, 'f'); m.put(5, 11, 'h')
m.put(25, 3, 'SSS'); m.put(25, 5, 'TT'); m.put(29, 3, '@'); m.put(28, 5, 'G'); m.put(29, 6, 'n')
m.put(25, 9, 'KK'); m.put(28, 11, '@'); m.put(26, 11, 'G'); m.put(29, 9, 'l'); m.put(30, 12, 'P')
m.put(4, 15, 'VV'); m.put(27, 15, 'VV'); m.at([(1, 14), (32, 14), (10, 15), (23, 15)], 'L')
m.at([(15, 15), (16, 15), (17, 15), (18, 15)], 's')
MAPS.append(('NEON ROOM', m.rows()))

# ---------------------------------------------------------------- 7 EMBASSY
m = Map('.')
m.fill(0, 14, 33, 16, ','); m.fill(15, 13, 18, 13, ',')
m.room(1, 1, 32, 12, 'm')
m.vwall(9, 1, 12); m.vwall(24, 1, 12)
m.hwall(1, 9, 6); m.hwall(24, 32, 6); m.hwall(9, 24, 5)
m.fill(10, 2, 23, 4, 'c')                       # ambassador's office
m.fill(10, 6, 23, 11, 'm')                      # grand hall
m.fill(2, 2, 8, 5, 'w'); m.fill(2, 7, 8, 11, 'w')
m.fill(25, 2, 31, 5, 'w'); m.fill(25, 7, 31, 11, 'c')
m.at([(16, 5), (17, 5)], 'R')                   # office double doors (reinforced)
m.at([(9, 3), (24, 3)], 'D')                    # side offices into ambassador's office
m.at([(9, 9), (24, 9), (5, 6), (28, 6)], 'D')
m.at([(16, 12), (17, 12)], 'D')
m.at([(12, 5)], '%'); m.at([(21, 5)], '%')
m.at([(4, 1), (12, 1), (16, 1), (17, 1), (21, 1), (29, 1), (1, 9), (32, 9), (5, 12), (28, 12)], 'W')
m.put(14, 2, 'KKKK'); m.put(16, 3, '@'); m.put(18, 3, 'G'); m.put(11, 2, 'P'); m.put(22, 2, 'P'); m.put(13, 4, 'l'); m.put(20, 4, 'l'); m.put(21, 3, 'g')
m.put(12, 8, 'P'); m.put(21, 8, 'P'); m.put(16, 8, 'l'); m.put(14, 10, 'e'); m.put(19, 7, 'g')
m.put(3, 3, 'HH'); m.put(6, 4, 'l'); m.put(7, 2, 'e'); m.put(3, 8, 'TT'); m.put(6, 10, 'l'); m.put(4, 10, 'g')
m.put(26, 3, 'KK'); m.put(30, 4, 'l'); m.put(29, 2, 'e'); m.put(26, 8, 'BB'); m.put(30, 10, '@'); m.put(28, 9, 'G'); m.put(30, 8, 'l')
m.at([(0, 13), (33, 13), (11, 15), (22, 15)], 'L')
m.at([(15, 15), (16, 15), (17, 15), (18, 15)], 's')
MAPS.append(('EMBASSY ROW', m.rows()))

# ---------------------------------------------------------------- 8 PENTHOUSE
m = Map(',')                                    # rooftop terrace around the suite
m.room(4, 1, 29, 13, 'm')
m.vwall(12, 1, 13); m.vwall(21, 1, 8)
m.hwall(4, 12, 7); m.hwall(21, 29, 8)
m.fill(5, 2, 11, 6, 'c'); m.fill(5, 8, 11, 12, 't')
m.fill(13, 2, 20, 12, 'w'); m.fill(22, 2, 28, 7, 'c'); m.fill(22, 9, 28, 12, 'm')
m.at([(12, 4), (12, 10), (21, 5), (25, 8)], 'D')
m.at([(16, 13)], 'D'); m.at([(29, 11)], 'D'); m.at([(4, 4)], 'D')
m.at([(8, 7)], '%')
m.at([(7, 1), (10, 1), (15, 1), (18, 1), (24, 1), (27, 1), (4, 10), (29, 4), (8, 13), (23, 13), (26, 13)], 'W')
m.put(6, 3, 'BB'); m.put(10, 2, '@'); m.put(9, 3, 'G'); m.put(10, 5, 'l')
m.put(6, 9, 'CC'); m.put(9, 11, 'f'); m.put(10, 9, 'e')
m.put(14, 4, 'SSS'); m.put(14, 6, 'TT'); m.put(18, 9, 'TT'); m.put(19, 3, 'l'); m.put(16, 11, 'l'); m.put(17, 6, 'g'); m.put(19, 11, 'e'); m.put(13, 12, 'P')
m.put(23, 3, 'BB'); m.put(27, 2, '@'); m.put(26, 4, 'G'); m.put(23, 6, 'l'); m.put(28, 6, 'h')
m.put(23, 10, 'TT'); m.put(27, 11, 'l'); m.put(26, 10, 'g')
m.at([(1, 1), (32, 1), (1, 14), (32, 14)], 'L'); m.at([(2, 6), (31, 7), (2, 12)], 'P')
m.at([(14, 15), (15, 15), (17, 15), (18, 15)], 's')
MAPS.append(('PENTHOUSE', m.rows()))

# ---------------------------------------------------------------- 9 FIRST CITY BANK
m = Map(',')
m.room(1, 1, 32, 13, 'm')
m.hwall(1, 32, 7)
m.vwall(9, 1, 7); m.vwall(17, 1, 7); m.vwall(24, 1, 7)
m.fill(2, 2, 8, 6, 'c'); m.fill(10, 2, 16, 6, 'c'); m.fill(18, 2, 23, 6, 'k'); m.fill(25, 2, 31, 6, 'k')
m.fill(2, 8, 31, 12, 'm')                       # lobby
m.at([(5, 7), (13, 7), (28, 7)], 'D')
m.at([(20, 7)], 'R')                            # vault door
m.at([(24, 4)], 'R')                            # vault to security room
m.at([(9, 4)], '%'); m.at([(17, 4)], '%')
m.at([(16, 13), (17, 13)], 'D'); m.at([(1, 10)], 'D'); m.at([(32, 10)], 'D')
m.at([(5, 13), (9, 13), (24, 13), (28, 13)], 'W')
m.put(3, 3, 'KK'); m.put(7, 5, 'l'); m.put(6, 2, 'e')
m.put(11, 3, 'KK'); m.put(15, 2, '@'); m.put(14, 3, 'G'); m.put(15, 5, 'l')
m.put(19, 2, 'XX'); m.put(22, 2, 'X'); m.put(20, 5, '@'); m.put(22, 4, 'G'); m.put(19, 4, 'f'); m.put(22, 6, 'X')
m.put(26, 2, 'HH'); m.put(29, 3, 'f'); m.put(30, 5, 'g'); m.put(26, 5, 'h')
m.put(4, 9, 'CCCCCCCCC'); m.put(8, 11, 'f'); m.put(16, 10, 'f'); m.put(24, 11, 'f')
m.put(20, 9, 'PP'); m.put(25, 9, 'SS'); m.put(6, 11, 'e'); m.put(14, 9, 'g'); m.put(22, 11, 'e'); m.put(29, 9, 'h')
m.at([(0, 14), (33, 14), (12, 15), (21, 15)], 'L')
m.at([(15, 15), (16, 15), (17, 15), (18, 15)], 's')
MAPS.append(('FIRST CITY BANK', m.rows()))

# ---------------------------------------------------------------- 10 THE MANSION
m = Map('.')
m.fill(0, 15, 33, 16, ','); m.fill(16, 13, 17, 14, ',')
m.room(0, 0, 33, 12, 'w')
m.vwall(7, 0, 12); m.vwall(14, 0, 6); m.vwall(19, 0, 6); m.vwall(26, 0, 12)
m.hwall(0, 7, 6); m.hwall(7, 26, 6); m.hwall(26, 33, 6)
m.fill(1, 1, 6, 5, 'c'); m.fill(1, 7, 6, 11, 't')
m.fill(8, 1, 13, 5, 'w'); m.fill(15, 1, 18, 5, 'm'); m.fill(20, 1, 25, 5, 'c')
m.fill(8, 7, 25, 11, 'm')                       # great hall
m.fill(27, 1, 32, 5, 'c'); m.fill(27, 7, 32, 11, 'w')
m.at([(3, 6), (10, 6), (23, 6), (30, 6), (7, 9), (26, 9)], 'D'); m.at([(16, 6), (17, 6)], 'R')
m.at([(16, 12), (17, 12)], 'D')
m.at([(7, 3), (26, 3)], '%'); m.at([(14, 3)], 'R'); m.at([(19, 3)], 'R')
m.at([(3, 0), (10, 0), (16, 0), (17, 0), (23, 0), (30, 0), (3, 12), (11, 12), (22, 12), (30, 12)], 'W')
m.put(2, 2, 'BB'); m.put(5, 2, '@'); m.put(4, 4, 'G'); m.put(1, 5, 'l')
m.put(2, 8, 'CC'); m.put(5, 10, 'f'); m.put(3, 10, 'e')
m.put(9, 2, 'HH'); m.put(12, 4, 'l'); m.put(11, 2, 'g')
m.put(16, 2, '@'); m.put(17, 4, 'G'); m.put(18, 1, 'l')          # safe room
m.put(21, 2, 'SS'); m.put(24, 4, 'l'); m.put(22, 4, 'e')
m.put(28, 2, 'BB'); m.put(31, 2, '@'); m.put(30, 4, 'G'); m.put(28, 5, 'l')
m.put(28, 8, 'TTT'); m.put(31, 10, 'l'); m.put(30, 11, 'g')
m.put(11, 8, 'P'); m.put(22, 8, 'P'); m.put(12, 10, 'SS'); m.put(20, 10, 'SS'); m.put(16, 8, 'l'); m.put(17, 10, 'l')
m.put(15, 9, 'h'); m.put(9, 11, 'e'); m.put(24, 10, 'g')
m.at([(1, 14), (32, 14), (9, 14), (24, 14)], 'L'); m.at([(5, 14), (28, 14)], 'P')
m.at([(14, 16), (15, 16), (18, 16), (19, 16)], 's')
MAPS.append(('THE MANSION', m.rows()))

ok = True
for name, rows in MAPS:
    ok &= check(name, rows)
if len(sys.argv) > 1 and sys.argv[1] == 'show':
    for name, rows in MAPS:
        print(name); print('\n'.join(rows)); print()
out = os.path.join(os.path.dirname(__file__), '..', 'source', 'maps.inc')
with open(out, 'w') as f:
    f.write('/* generated by tools/maps.py - edit that, not this */\n')
    for i, (name, rows) in enumerate(MAPS):
        f.write('#define MAP%d \\\n' % (i + 1))
        f.write(' \\\n'.join('    "%s",' % r for r in rows) + '\n')
print('maps ok' if ok else 'MAP ERRORS')
sys.exit(0 if ok else 1)
