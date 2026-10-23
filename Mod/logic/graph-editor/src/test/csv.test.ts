import { describe, expect, it } from 'vitest';
import { parseCsvDocument, stringifyCsv } from '../lib/csv';

describe('csv helpers', () => {
  it('parses quoted fields and embedded commas', () => {
    const document = parseCsvDocument('id,name,notes\n1,"Moon, Good","Line ""quoted"""\n');

    expect(document.headers).toEqual(['id', 'name', 'notes']);
    expect(document.rows).toEqual([
      {
        id: '1',
        name: 'Moon, Good',
        notes: 'Line "quoted"'
      }
    ]);
  });

  it('round-trips values that need escaping', () => {
    const rows = [{ id: '1', expr: 'Frog | (TripleJump & Dive)', notes: 'a,b "c"' }];
    const csv = stringifyCsv(rows, ['id', 'expr', 'notes']);

    expect(parseCsvDocument(csv).rows).toEqual(rows);
  });
});
