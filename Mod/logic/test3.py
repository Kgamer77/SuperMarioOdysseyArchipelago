import sys
sys.path.append('.')
from generate_logic_templates import parse_stage_points
from collections import defaultdict

stage_points = parse_stage_points()
stage_change_points = defaultdict(list)
for point in stage_points:
    if point["point_type"] == "stage_change":
        stage_change_points[(point["stage"], point["identifier"])].append(point)

print("jizo01 in SkyWorldHomeStage:", stage_change_points.get(("SkyWorldHomeStage", "jizo01")))
print("jizo01 in SkyWorldCastleZone:", stage_change_points.get(("SkyWorldCastleZone", "jizo01")))
print("RaceEntrance in SnowWorldTownStage:", stage_change_points.get(("SnowWorldTownStage", "RaceEntrance")))
