import enemyEntitiesCsv from '../../../../config/enemy_swap_entities.csv?raw';
import enemyPlacementsCsv from '../../../../config/enemy_capture_map.csv?raw';
import enemyRulesCsv from '../../../../config/enemy_swap_rules.csv?raw';
import { parseCsvDocument } from '../lib/csv';
import type {
  EnemySwapEntity,
  EnemySwapPlacement,
  EnemySwapRule
} from '../lib/types';

export const enemySwapEntities =
  parseCsvDocument<EnemySwapEntity>(enemyEntitiesCsv).rows;
export const enemySwapPlacements =
  parseCsvDocument<EnemySwapPlacement>(enemyPlacementsCsv).rows;
export const enemySwapRules =
  parseCsvDocument<EnemySwapRule>(enemyRulesCsv).rows;

export const enemyEntityByName = new Map(
  enemySwapEntities.map((entity) => [entity.unit_config_name, entity])
);
export const enemyRuleByPlacement = new Map(
  enemySwapRules.map((rule) => [`${rule.stage}::${rule.object_id}`, rule])
);

function enabled(value: string): boolean {
  return ['1', 'true', 'yes', 'y'].includes((value || '').trim().toLowerCase());
}

export function friendlyNameForPlacement(placement: EnemySwapPlacement): string {
  return enemyEntityByName.get(placement.unit_config_name)?.friendly_name
    || placement.unit_config_name
    || '(unknown actor)';
}

export function categoryForPlacement(placement: EnemySwapPlacement): string {
  return enemyEntityByName.get(placement.unit_config_name)?.category || 'other';
}

export function captureTokenForPlacement(placement: EnemySwapPlacement): string {
  return enemyEntityByName.get(placement.unit_config_name)?.capture_token || '';
}

export function placementIsSwapEligible(placement: EnemySwapPlacement): boolean {
  const entity = enemyEntityByName.get(placement.unit_config_name);
  const rule = enemyRuleByPlacement.get(`${placement.stage}::${placement.object_id}`);
  const policy = (rule?.policy || 'keep').trim().toLowerCase();
  const eventName = (placement.event_name || '').trim().toLowerCase();
  const runtimeSourceSafe = entity?.category !== 'npc'
    || !eventName
    || eventName === 'none'
    || eventName.includes('onlyballoon');
  return Boolean(entity && enabled(entity.swap_out)
    && runtimeSourceSafe
    && ['randomize', 'logic', 'any'].includes(policy)
    && ['capture', 'enemy', 'npc', 'prop'].includes(entity.category));
}

export const safeCaptureTargets = enemySwapEntities
  .filter((entity) =>
    Boolean(entity.capture_token)
    && enabled(entity.any_pool)
    && ['capture', 'enemy', 'generator'].includes(entity.category)
  )
  .sort((left, right) =>
    left.friendly_name.localeCompare(right.friendly_name)
    || left.unit_config_name.localeCompare(right.unit_config_name)
  );

export const safeCaptureTokens = [...new Map(
  safeCaptureTargets.map((entity) => [entity.capture_token, entity])
).entries()]
  .map(([token, entity]) => ({
    token,
    friendlyName: entity.friendly_name,
    unitConfigName: entity.unit_config_name
  }))
  .sort((left, right) => left.friendlyName.localeCompare(right.friendlyName));

const producibleCaptureTokens = new Set(
  enemySwapEntities
    .map((entity) => entity.capture_token)
    .filter(Boolean)
);
const swappableCaptureTokenSet = new Set(
  enemySwapEntities
    .filter((entity) => enabled(entity.swap_out))
    .map((entity) => entity.capture_token)
    .filter(Boolean)
);

export const reliableCaptureTokens = new Set(
  [...producibleCaptureTokens]
    .filter((token) => !swappableCaptureTokenSet.has(token))
);

export const swappableCaptureTokens = new Set(
  [...producibleCaptureTokens]
    .filter((token) => !reliableCaptureTokens.has(token))
);

const stageLockedCaptureKeys = new Set([
  'KaronWingTowerStage::KaronWing'
]);

export function captureIsStageLocked(
  logicStage: string,
  captureToken: string
): boolean {
  return stageLockedCaptureKeys.has(`${logicStage}::${captureToken}`);
}
