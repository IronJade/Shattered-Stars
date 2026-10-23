from test.bases import WorldTestBase

from ..items import GAME
from ..world import SO2World


class SO2TestBase(WorldTestBase):
    game = GAME
    world: SO2World
