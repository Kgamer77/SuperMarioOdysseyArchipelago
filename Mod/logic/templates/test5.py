import csv
from collections import defaultdict

def test():
    stage_points = list(csv.DictReader(open('raw_stage_points.csv')))
    entrances = list(csv.DictReader(open('entrance_links.csv')))
    
    stage_change_points = defaultdict(list)
    for point in stage_points:
        if point["point_type"] == "stage_change":
            stage_change_points[(point["stage"], point["identifier"])].append(point)
            
    def stage_change_anchor(stage: str, ident: str) -> dict[str, str]:
        points = stage_change_points.get((stage, ident), [])
        if not points:
            return {"obj_id": "", "x": "", "y": "", "z": "", "scenario_indices": "", "notes": ""}
        scenario_indices = set()
        obj_ids = set()
        for point in points:
            scenario_indices.update(filter(None, point["scenario_indices"].split(";")))
            if point["obj_id"]:
                obj_ids.add(point["obj_id"])
        return {
            "obj_id": ";".join(sorted(obj_ids)),
            "scenario_indices": ";".join(sorted(scenario_indices, key=int)) if scenario_indices else "",
        }
    
    for link in entrances:
        if link["home_stage"] == "SnowWorldTownStage" and link["home_entrance_in"] == "RaceEntrance":
            ident = link["home_entrance_in"]
            anchor = stage_change_anchor(link["home_stage"], ident)
            print("home:", anchor)
            
            ident = link["sub_entrance_in"]
            anchor = stage_change_anchor(link["sub_stage"], ident)
            print("sub:", anchor)

test()
