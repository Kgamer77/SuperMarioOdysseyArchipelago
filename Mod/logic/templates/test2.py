import csv
with open('logic_nodes.csv') as f:
    for row in csv.DictReader(f):
        if row['node_id'] == 'SnowWorldTownStage::entrance::RaceEntrance::home_in':
            print(row)
