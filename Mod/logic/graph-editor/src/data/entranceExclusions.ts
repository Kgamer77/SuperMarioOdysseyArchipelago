import entranceLinksCsv from '../../../templates/entrance_links.csv?raw';
import { parseCsvDocument } from '../lib/csv';

const NODE_ID_COLUMNS = [
  'home_in_node_id',
  'home_out_node_id',
  'sub_near_node_id',
  'sub_far_node_id'
] as const;

export interface DoorNodePair {
  home: string;
  sub: string;
}

interface EntranceLinkIndex {
  excluded: Set<string>;
  activeHomeIn: Set<string>;
  activeHomeOut: Set<string>;
  doorPairs: DoorNodePair[];
}

function buildEntranceLinkIndex(): EntranceLinkIndex {
  const excluded = new Set<string>();
  const activeHomeIn = new Set<string>();
  const activeHomeOut = new Set<string>();
  const doorPairs: DoorNodePair[] = [];
  const { rows } = parseCsvDocument(entranceLinksCsv);
  for (const row of rows) {
    const status = (row.status ?? '').trim();
    // Only `excluded` removes a door from the logic graph. `vanilla` is a fixed,
    // non-randomized loading zone that is still a real connection (this mirrors
    // the runtime generator, which marks a node `excluded` solely when
    // status == 'excluded'). Treating vanilla as excluded would falsely report
    // its sub-area as unreachable from the overworld.
    if (status === 'excluded') {
      for (const column of NODE_ID_COLUMNS) {
        const id = (row[column] ?? '').trim();
        if (id) {
          excluded.add(id);
        }
      }
      continue;
    }
    // Active and vanilla links are both genuine doors: the home_in side is the
    // enterable door and the home_out side is the return/exit. A door can show up
    // as in for one link and out for another (e.g. shared pipes) — those end up in
    // both sets and read as "both". (Only `active` links are randomizable, but the
    // randomizable check is made separately against the status column.)
    const homeIn = (row.home_in_node_id ?? '').trim();
    const homeOut = (row.home_out_node_id ?? '').trim();
    if (homeIn) {
      activeHomeIn.add(homeIn);
    }
    if (homeOut) {
      activeHomeOut.add(homeOut);
    }
    // The link is the authoritative home<->sub pairing for a door, which is the
    // only way to connect a door whose two sides use different entrance_ids
    // (e.g. Wooded Tower001/Tower002, Snow RaceEntrance/RaceTrackExit). Reachability
    // groups doors by entrance_id, so those asymmetric doors never pair without
    // this. home_in mates with sub_near, home_out with sub_far.
    const subNear = (row.sub_near_node_id ?? '').trim();
    const subFar = (row.sub_far_node_id ?? '').trim();
    if (homeIn && subNear) {
      doorPairs.push({ home: homeIn, sub: subNear });
    }
    if (homeOut && subFar) {
      doorPairs.push({ home: homeOut, sub: subFar });
    }
  }
  return { excluded, activeHomeIn, activeHomeOut, doorPairs };
}

const ENTRANCE_LINK_INDEX = buildEntranceLinkIndex();

// Entrance endpoints that are excluded from the randomizer, derived live from the
// config-generated entrance_links.csv (every link whose `status` is not "active").
// This is the single source of truth for exclusion: it is intentionally NOT stored
// in kingdom saves, so it can never go stale the way a frozen
// `source: EntranceDB:excluded` field on a saved node did.
export const EXCLUDED_ENTRANCE_NODE_IDS: ReadonlySet<string> = ENTRANCE_LINK_INDEX.excluded;

// Home-side endpoints that an active link actually uses as the enterable side
// (home_in) or the return/exit side (home_out). EntranceDB generates both a
// home_in and a home_out node for every entrance_id, but the config decides which
// side is real — so these sets, not node presence, tell us a door's true role.
export const ACTIVE_HOME_IN_NODE_IDS: ReadonlySet<string> = ENTRANCE_LINK_INDEX.activeHomeIn;
export const ACTIVE_HOME_OUT_NODE_IDS: ReadonlySet<string> = ENTRANCE_LINK_INDEX.activeHomeOut;

// Authoritative home<->sub door connections from every non-excluded link, used by
// reachability so doors whose two sides use different entrance_ids still pair.
export const ENTRANCE_DOOR_NODE_PAIRS: ReadonlyArray<DoorNodePair> = ENTRANCE_LINK_INDEX.doorPairs;

export function isExcludedEntranceNode(nodeId: string | undefined): boolean {
  return !!nodeId && EXCLUDED_ENTRANCE_NODE_IDS.has(nodeId);
}

export function isActiveEntranceIn(nodeId: string | undefined): boolean {
  return !!nodeId && ACTIVE_HOME_IN_NODE_IDS.has(nodeId);
}

export function isActiveEntranceOut(nodeId: string | undefined): boolean {
  return !!nodeId && ACTIVE_HOME_OUT_NODE_IDS.has(nodeId);
}
