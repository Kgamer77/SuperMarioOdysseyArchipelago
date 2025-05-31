from . import SMOTestBase
from ..Items import story_moons, multi_moons

class TestStoryPlacement(SMOTestBase):
    options = {
        "goal": "moon"
    }

    def test_sand(self) -> None:
        """Test region connections"""
        locations = story_moons["Sand"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Sand Story Moon")

        locations = multi_moons["Sand"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Sand Multi-Moon")

    def test_lake(self) -> None:
        """Test region connections"""
        locations = multi_moons["Lake"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Lake Multi-Moon")


    def test_wooded(self) -> None:
        """Test region connections"""
        locations = story_moons["Wooded"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Wooded Story Moon")

        locations = multi_moons["Wooded"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Wooded Multi-Moon")

    def test_metro(self) -> None:
        """Test region connections"""
        locations = story_moons["Metro"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Metro Story Moon")

        locations = multi_moons["Metro"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Metro Multi-Moon")


    def test_snow(self) -> None:
        """Test region connections"""
        locations = story_moons["Snow"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Snow Story Moon")

        locations = multi_moons["Snow"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Snow Multi-Moon")

    def test_seaside(self) -> None:
        """Test region connections"""
        locations = story_moons["Seaside"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Seaside Story Moon")

        locations = multi_moons["Seaside"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Seaside Multi-Moon")

    def test_luncheon(self) -> None:
        """Test region connections"""
        locations = story_moons["Luncheon"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Luncheon Story Moon")

        locations = multi_moons["Luncheon"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Luncheon Multi-Moon")

    def test_bowser(self) -> None:
        """Test region connections"""
        locations = story_moons["Bowser"]
        location_contents = []
        for location in locations:
            self.assertTrue(location in self.multiworld.regions.location_cache)
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Bowser Story Moon")

        locations = multi_moons["Bowser"]
        location_contents = []
        for location in locations:
            location_contents.append(self.multiworld.get_location(location, self.player).item.name)
        for item in location_contents:
            self.assertEqual(item, "Bowser Multi-Moon")