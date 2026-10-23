import { describe, expect, it } from 'vitest';
import {
  addAlternative,
  parseRequirementExpression,
  removeAlternative,
  stringifyRequirement,
  toggleNodeInAlternative,
  toggleTokenInAlternative
} from '../lib/requirements';
import { normalizeRouteDifficulties, routeDifficultyForOption } from '../lib/routeDifficulty';

const tokenSet = new Set([
  'Frog',
  'TripleJump',
  'Vault',
  'WallJump',
  'Dive',
  'LongJump',
  'WorldPeace_Cap',
  'MoonRockActive_Cap',
  'free',
  'impossible'
]);

const nodeSet = new Set([
  'CapWorldHomeStage::start',
  'CapWorldHomeStage::checkpoint::obj12'
]);

describe('requirement expressions', () => {
  it('parses OR alternatives with AND groups', () => {
    const model = parseRequirementExpression(
      'Frog | (TripleJump & Vault & WallJump & Dive)',
      tokenSet
    );

    expect(model.mode).toBe('custom');
    expect(model.alternatives).toEqual([
      ['Frog'],
      ['TripleJump', 'Vault', 'WallJump', 'Dive']
    ]);
  });

  it('normalizes nested OR expressions into DNF', () => {
    const model = parseRequirementExpression('LongJump & (Frog | Dive)', tokenSet);

    expect(model.alternatives).toEqual([
      ['LongJump', 'Frog'],
      ['LongJump', 'Dive']
    ]);
    expect(stringifyRequirement(model)).toBe('(LongJump & Frog) | (LongJump & Dive)');
  });

  it('keeps unknown tokens in raw mode with errors', () => {
    const model = parseRequirementExpression('Frog | MissingCapture', tokenSet);

    expect(model.mode).toBe('raw');
    expect(model.errors).toEqual(['Unknown token: MissingCapture']);
  });

  it('builds expressions by toggling tokens inside alternatives', () => {
    let expression = '';
    expression = toggleTokenInAlternative(expression, 0, 'Frog', tokenSet);
    expression = addAlternative(expression, tokenSet);
    expression = toggleTokenInAlternative(expression, 1, 'TripleJump', tokenSet);
    expression = toggleTokenInAlternative(expression, 1, 'Vault', tokenSet);

    expect(expression).toBe('Frog | (TripleJump & Vault)');
  });

  it('defaults missing route option difficulties to casual', () => {
    expect(normalizeRouteDifficulties(undefined, 2)).toEqual(['casual', 'casual']);
    expect(routeDifficultyForOption(undefined, 0)).toBe('casual');
  });
});

describe('node access requirements', () => {
  it('splits @node atoms out of the token alternatives', () => {
    const model = parseRequirementExpression(
      'Frog & @CapWorldHomeStage::checkpoint::obj12 | Dive',
      tokenSet,
      nodeSet
    );

    expect(model.mode).toBe('custom');
    expect(model.alternatives).toEqual([['Frog'], ['Dive']]);
    expect(model.nodeAlternatives).toEqual([
      ['CapWorldHomeStage::checkpoint::obj12'],
      []
    ]);
  });

  it('accepts an option that needs only node access', () => {
    const model = parseRequirementExpression('@CapWorldHomeStage::start', tokenSet, nodeSet);

    expect(model.mode).toBe('custom');
    expect(model.alternatives).toEqual([[]]);
    expect(model.nodeAlternatives).toEqual([['CapWorldHomeStage::start']]);
  });

  it('reports an unknown node when a node table is supplied', () => {
    const model = parseRequirementExpression('Frog & @Nope::start', tokenSet, nodeSet);

    expect(model.mode).toBe('raw');
    expect(model.errors).toEqual(['Unknown node: Nope::start']);
  });

  it('leaves node atoms unchecked when no node table is supplied', () => {
    const model = parseRequirementExpression('Frog & @Nope::start', tokenSet);

    expect(model.mode).toBe('custom');
    expect(model.nodeAlternatives).toEqual([['Nope::start']]);
  });

  it('round-trips node access through stringify', () => {
    const model = parseRequirementExpression(
      'Frog & @CapWorldHomeStage::start', tokenSet, nodeSet
    );

    // Multi-atom options are always parenthesised, node atoms included.
    expect(stringifyRequirement(model)).toBe('(Frog & @CapWorldHomeStage::start)');
  });

  it('toggles node access per OR option', () => {
    let expression = '';
    expression = toggleTokenInAlternative(expression, 0, 'Frog', tokenSet);
    expression = toggleNodeInAlternative(
      expression, 0, 'CapWorldHomeStage::start', tokenSet
    );
    expression = addAlternative(expression, tokenSet);
    expression = toggleTokenInAlternative(expression, 1, 'Dive', tokenSet);

    expect(expression).toBe('(Frog & @CapWorldHomeStage::start) | Dive');

    expression = toggleNodeInAlternative(
      expression, 0, 'CapWorldHomeStage::start', tokenSet
    );
    expect(expression).toBe('Frog | Dive');
  });

  it('keeps node lists aligned when an option is removed', () => {
    const expression = 'Frog | (Dive & @CapWorldHomeStage::start)';

    expect(removeAlternative(expression, 0, tokenSet)).toBe(
      '(Dive & @CapWorldHomeStage::start)'
    );
  });
});
