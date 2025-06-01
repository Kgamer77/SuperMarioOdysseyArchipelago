from . import SMOTestBase
from ..Locations import loc_Sand_Peace, loc_Wooded_Post_Story1, loc_Wooded_Peace, \
    loc_Metro_Sewer_Access, loc_Metro_Peace, loc_Luncheon_Post_Spewart, \
    loc_Luncheon_Post_Cheese_Rocks, loc_Luncheon_Peace, loc_Metro_Post_Sand, loc_Sand_Pyramid


class TestRegionAccess(SMOTestBase):
    options = {
        "goal": "moon",
        "counts": "same_total",
    }

    def test_sand(self) -> None:
        """Test region connections"""
        locations = list({**loc_Sand_Peace, **loc_Metro_Post_Sand, **loc_Sand_Pyramid}.keys())
        items = [["Sand Story Moon"]]
        self.assertAccessDependency(locations, items)

    def test_wooded(self) -> None:
        """Test region connections"""
        # locations = list(loc_Wooded_Post_Story1.keys())
        # items = [["Wooded Story Moon"]]
        # self.assertAccessDependency(locations, items)
        locations = list(loc_Wooded_Peace.keys())
        items = [["Wooded Story Moon","Wooded Story Moon"]]
        self.assertAccessDependency(locations, items)

    def test_metro(self) -> None:
        """Test region connections"""
        locations = list(loc_Metro_Sewer_Access.keys())
        items = [["Metro Story Moon", "Metro Story Moon", "Metro Story Moon", "Metro Story Moon"]]
        self.assertAccessDependency(locations, items)
        locations = list(loc_Metro_Peace.keys())
        items = [["Metro Story Moon", "Metro Story Moon", "Metro Story Moon", "Metro Story Moon", "Metro Story Moon"]]
        self.assertAccessDependency(locations, items)

    def test_luncheon(self) -> None:
        """Test region connections"""
        locations = list(loc_Luncheon_Post_Spewart.keys())
        items = [["Luncheon Story Moon"]]
        self.assertAccessDependency(locations, items)
        locations = list(loc_Luncheon_Post_Cheese_Rocks.keys())
        items = [["Luncheon Story Moon", "Luncheon Story Moon"]]
        self.assertAccessDependency(locations, items)
        locations = list(loc_Luncheon_Peace.keys())
        items = [["Luncheon Story Moon", "Luncheon Story Moon", "Luncheon Story Moon"]]
        self.assertAccessDependency(locations, items)