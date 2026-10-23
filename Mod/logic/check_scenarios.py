#!/usr/bin/env python3
from randomizer_logic import SCENARIO_START_BY_KINGDOM, SCENARIO_MOON_ROCK_BY_KINGDOM

print('Starting scenarios:')
for kingdom in ['Cap Kingdom', 'Cascade Kingdom', 'Moon Kingdom']:
    print(f'  {kingdom}: scenario {SCENARIO_START_BY_KINGDOM.get(kingdom, 1)}')

print()
print('Moon Rock scenarios:')
for kingdom in ['Cap Kingdom', 'Cascade Kingdom', 'Moon Kingdom']:
    print(f'  {kingdom}: scenario {SCENARIO_MOON_ROCK_BY_KINGDOM.get(kingdom, "N/A")}')
