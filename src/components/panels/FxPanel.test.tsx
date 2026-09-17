import { expect, it } from 'vitest';
import { renderToStaticMarkup } from 'react-dom/server';
import { FxPanel } from './FxPanel';

it('gives WT-1 Drive and Chorus independent rack grid slots', () => {
  const html = renderToStaticMarkup(<FxPanel />);
  expect(html).toContain('grid-area:drive');
  expect(html).toContain('grid-area:chorus');
  expect(html).not.toContain('panel-fx');
  expect(html).not.toContain('fx-module');
});
