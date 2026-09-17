import test from 'node:test';
import assert from 'node:assert/strict';
import {matchesCountry} from '../web/modules/country-search.js';
test('country search matches localized and original names without case or accent sensitivity',()=>{
 assert.ok(matchesCountry('Германия (7)','Germany','гЕрМ'));
 assert.ok(matchesCountry('Германия (7)','Germany','GERM'));
 assert.ok(matchesCountry('Türkiye (7)','Turkey','turkiye'));
 assert.ok(matchesCountry('Österreich','Austria',''));
 assert.equal(matchesCountry('France','France','Germany'),false);
});
