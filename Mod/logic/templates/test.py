import csv
from collections import defaultdict
points = list(csv.DictReader(open('raw_stage_points.csv')))
stage_change_points = defaultdict(list)
for p in points:
    if p['point_type'] == 'stage_change':
        stage_change_points[(p['stage'], p['identifier'])].append(p)
print(len(stage_change_points))
print(stage_change_points.get(('SnowWorldTownStage', 'RaceEntrance')))
