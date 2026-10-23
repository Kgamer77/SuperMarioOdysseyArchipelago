import type { LogicNode, LogicToken, RequirementModel } from './types';

type TokenSet = Set<string>;

type AstNode =
  | { type: 'token'; value: string }
  | { type: 'and'; left: AstNode; right: AstNode }
  | { type: 'or'; left: AstNode; right: AstNode };

interface ParseState {
  tokens: string[];
  index: number;
}

// Node-access atoms are written `@<node_id>` in the expression. Node ids carry
// ':' separators, which is why they need a prefix instead of being plain
// identifiers - the tokenizer would otherwise stop at the first colon.
export const NODE_REQUIREMENT_PREFIX = '@';

export function isNodeRequirementAtom(atom: string): boolean {
  return atom.startsWith(NODE_REQUIREMENT_PREFIX);
}

export function nodeRequirementAtom(nodeId: string): string {
  return `${NODE_REQUIREMENT_PREFIX}${nodeId}`;
}

/** Short label for a node prerequisite, for canvas edge captions.
 *
 * Node ids are stage-prefixed, so the prefix is dropped when the requirement
 * points inside the stage the caption already belongs to. */
export function nodeRequirementLabel(nodeId: string, contextStage?: string): string {
  const local = contextStage && nodeId.startsWith(`${contextStage}::`)
    ? nodeId.slice(contextStage.length + 2)
    : nodeId;
  return `reach ${local}`;
}

export function parseRequirementExpression(
  expression: string,
  availableTokens: Iterable<string>,
  availableNodes?: Iterable<string>
): RequirementModel {
  const raw = expression.trim();
  const tokenSet = new Set(availableTokens);
  const nodeSet = availableNodes ? new Set(availableNodes) : undefined;

  if (raw === '') {
    return { mode: 'unset', alternatives: [], nodeAlternatives: [], raw: expression, errors: [] };
  }

  if (raw.toLowerCase() === 'free') {
    return { mode: 'free', alternatives: [], nodeAlternatives: [], raw: expression, errors: [] };
  }

  if (raw.toLowerCase() === 'impossible') {
    return { mode: 'impossible', alternatives: [], nodeAlternatives: [], raw: expression, errors: [] };
  }

  try {
    const state = { tokens: tokenizeRequirement(raw), index: 0 };
    const ast = parseOr(state);
    if (state.index !== state.tokens.length) {
      throw new Error(`Unexpected token "${state.tokens[state.index]}".`);
    }

    const dnf = astToDnf(ast).map((alternative) => unique(alternative));
    const alternatives = dnf.map((alternative) =>
      alternative.filter((atom) => !isNodeRequirementAtom(atom))
    );
    const nodeAlternatives = dnf.map((alternative) =>
      alternative.filter(isNodeRequirementAtom).map((atom) => atom.slice(1))
    );

    const unknownTokens = alternatives.flat().filter((token) => !tokenSet.has(token));
    const unknownNodes = nodeSet
      ? nodeAlternatives.flat().filter((node) => !nodeSet.has(node))
      : [];
    const errors = [
      ...unique(unknownTokens).map((token) => `Unknown token: ${token}`),
      ...unique(unknownNodes).map((node) => `Unknown node: ${node}`)
    ];

    return {
      mode: errors.length > 0 ? 'raw' : 'custom',
      alternatives,
      nodeAlternatives,
      raw: expression,
      errors
    };
  } catch (error) {
    return {
      mode: 'raw',
      alternatives: [],
      nodeAlternatives: [],
      raw: expression,
      errors: [(error as Error).message]
    };
  }
}

export function stringifyRequirement(model: RequirementModel): string {
  if (model.mode === 'unset') {
    return '';
  }

  if (model.mode === 'free') {
    return 'free';
  }

  if (model.mode === 'impossible') {
    return 'impossible';
  }

  if (model.mode === 'raw') {
    return model.raw;
  }

  const alternatives = model.alternatives
    .map((alternative, index) =>
      unique([
        ...alternative,
        ...(model.nodeAlternatives[index] ?? []).map(nodeRequirementAtom)
      ]).filter(Boolean)
    )
    .filter((alternative) => alternative.length > 0);

  if (alternatives.length === 0) {
    return '';
  }

  return alternatives
    .map((alternative) => {
      if (alternative.length === 1) {
        return alternative[0];
      }
      return `(${alternative.join(' & ')})`;
    })
    .join(' | ');
}

export function setRequirementMode(
  expression: string,
  mode: RequirementModel['mode'],
  tokenSet: Iterable<string>
): string {
  if (mode === 'unset') {
    return '';
  }

  if (mode === 'free') {
    return 'free';
  }

  if (mode === 'impossible') {
    return 'impossible';
  }

  const current = parseRequirementExpression(expression, tokenSet);
  if (current.mode === 'custom' && current.alternatives.length > 0) {
    return stringifyRequirement({ ...current, mode: 'custom' });
  }

  return stringifyRequirement({
    mode: 'custom',
    alternatives: [[]],
    nodeAlternatives: [[]],
    raw: '',
    errors: []
  });
}

export function toggleTokenInAlternative(
  expression: string,
  alternativeIndex: number,
  token: string,
  tokenSet: Iterable<string>
): string {
  const model = ensureCustomModel(expression, tokenSet);
  growAlternatives(model, alternativeIndex);

  const alternative = model.alternatives[alternativeIndex];
  const existingIndex = alternative.indexOf(token);
  if (existingIndex >= 0) {
    alternative.splice(existingIndex, 1);
  } else {
    alternative.push(token);
  }

  return stringifyRequirement(model);
}

export function toggleNodeInAlternative(
  expression: string,
  alternativeIndex: number,
  nodeId: string,
  tokenSet: Iterable<string>
): string {
  const model = ensureCustomModel(expression, tokenSet);
  growAlternatives(model, alternativeIndex);

  const nodes = model.nodeAlternatives[alternativeIndex];
  const existingIndex = nodes.indexOf(nodeId);
  if (existingIndex >= 0) {
    nodes.splice(existingIndex, 1);
  } else {
    nodes.push(nodeId);
  }

  return stringifyRequirement(model);
}

export function addAlternative(expression: string, tokenSet: Iterable<string>): string {
  const model = ensureCustomModel(expression, tokenSet);
  model.alternatives.push([]);
  model.nodeAlternatives.push([]);
  return stringifyRequirement(model);
}

export function removeAlternative(
  expression: string,
  alternativeIndex: number,
  tokenSet: Iterable<string>
): string {
  const model = ensureCustomModel(expression, tokenSet);
  model.alternatives.splice(alternativeIndex, 1);
  model.nodeAlternatives.splice(alternativeIndex, 1);
  return stringifyRequirement(model);
}

export function describeRequirement(
  expression: string,
  tokens: LogicToken[],
  nodes: LogicNode[] = []
): string {
  const byToken = new Map(tokens.map((token) => [token.token, token.display_name || token.token]));
  const byNode = new Map(nodes.map((node) => [node.node_id, node.display_name || node.node_id]));
  const model = parseRequirementExpression(expression, byToken.keys());

  if (model.mode === 'unset') {
    return 'todo';
  }

  if (model.mode === 'free') {
    return 'free';
  }

  if (model.mode === 'impossible') {
    return 'impossible';
  }

  if (model.mode === 'raw') {
    return expression || 'invalid';
  }

  return model.alternatives
    .map((alternative, index) =>
      [
        ...alternative.map((token) => byToken.get(token) ?? token),
        ...(model.nodeAlternatives[index] ?? []).map(
          (node) => (byNode.has(node) ? `reach ${byNode.get(node)}` : nodeRequirementLabel(node))
        )
      ].join(' + ')
    )
    .join(' OR ');
}

export function countRequirementOptions(expression: string): number {
  const raw = expression.trim();
  if (!raw || raw.toLowerCase() === 'free' || raw.toLowerCase() === 'impossible') {
    return 1;
  }

  try {
    const identifiers = tokenizeRequirement(raw).filter((token) => !['(', ')', '&', '|'].includes(token));
    const model = parseRequirementExpression(raw, identifiers);
    return model.mode === 'custom' ? Math.max(1, model.alternatives.length) : 1;
  } catch {
    return 1;
  }
}

export function tokenOptionsByType(tokens: LogicToken[], allowedTypes: string[]): LogicToken[] {
  return tokens
    .filter((token) => allowedTypes.includes(token.token_type))
    .filter((token) => token.token !== 'free' && token.token !== 'impossible')
    .sort((a, b) => {
      const categoryCompare = a.category.localeCompare(b.category);
      if (categoryCompare !== 0) {
        return categoryCompare;
      }
      return (a.display_name || a.token).localeCompare(b.display_name || b.token);
    });
}

function growAlternatives(model: RequirementModel, alternativeIndex: number): void {
  while (model.alternatives.length <= alternativeIndex) {
    model.alternatives.push([]);
  }
  while (model.nodeAlternatives.length < model.alternatives.length) {
    model.nodeAlternatives.push([]);
  }
}

function ensureCustomModel(expression: string, tokenSet: Iterable<string>): RequirementModel {
  const model = parseRequirementExpression(expression, tokenSet);
  if (model.mode === 'custom') {
    return {
      mode: 'custom',
      alternatives: model.alternatives.map((alternative) => [...alternative]),
      nodeAlternatives: model.alternatives.map(
        (_alternative, index) => [...(model.nodeAlternatives[index] ?? [])]
      ),
      raw: '',
      errors: []
    };
  }

  return {
    mode: 'custom',
    alternatives: [[]],
    nodeAlternatives: [[]],
    raw: '',
    errors: []
  };
}

function tokenizeRequirement(expression: string): string[] {
  const tokens: string[] = [];
  const pattern = /\s*(@[A-Za-z0-9_:]+|[A-Za-z0-9_]+|[()&|])\s*/gy;
  let index = 0;

  while (index < expression.length) {
    pattern.lastIndex = index;
    const match = pattern.exec(expression);
    if (!match) {
      throw new Error(`Invalid character near "${expression.slice(index, index + 10)}".`);
    }

    tokens.push(match[1]);
    index = pattern.lastIndex;
  }

  return tokens;
}

function parseOr(state: ParseState): AstNode {
  let node = parseAnd(state);
  while (state.tokens[state.index] === '|') {
    state.index += 1;
    node = { type: 'or', left: node, right: parseAnd(state) };
  }
  return node;
}

function parseAnd(state: ParseState): AstNode {
  let node = parsePrimary(state);
  while (state.tokens[state.index] === '&') {
    state.index += 1;
    node = { type: 'and', left: node, right: parsePrimary(state) };
  }
  return node;
}

function parsePrimary(state: ParseState): AstNode {
  const token = state.tokens[state.index];
  if (!token) {
    throw new Error('Unexpected end of expression.');
  }

  if (token === '(') {
    state.index += 1;
    const node = parseOr(state);
    if (state.tokens[state.index] !== ')') {
      throw new Error('Missing closing parenthesis.');
    }
    state.index += 1;
    return node;
  }

  if (token === ')' || token === '&' || token === '|') {
    throw new Error(`Unexpected token "${token}".`);
  }

  state.index += 1;
  return { type: 'token', value: token };
}

function astToDnf(ast: AstNode): string[][] {
  if (ast.type === 'token') {
    return [[ast.value]];
  }

  if (ast.type === 'or') {
    return [...astToDnf(ast.left), ...astToDnf(ast.right)];
  }

  const left = astToDnf(ast.left);
  const right = astToDnf(ast.right);
  const combined: string[][] = [];

  for (const leftAlternative of left) {
    for (const rightAlternative of right) {
      combined.push(unique([...leftAlternative, ...rightAlternative]));
    }
  }

  return combined;
}

function unique<T>(values: T[]): T[] {
  return [...new Set(values)];
}

export function collectTokenSet(tokens: LogicToken[]): TokenSet {
  return new Set(tokens.map((token) => token.token));
}

export function collectNodeSet(nodes: LogicNode[]): Set<string> {
  return new Set(nodes.map((node) => node.node_id));
}
