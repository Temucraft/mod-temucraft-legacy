-- mod-temucraft-legacy: player-facing texts. Translations go in module_string_locale (same module and id, plus the locale).
DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-legacy';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-legacy', 1, '|cffff8000Legacy challenge complete:|r {}. |cffff8000+1 Legacy point|r (every character on your account can spend it; .legacy).'),
('mod-temucraft-legacy', 2, '|cff8a4b00Legacy points: {} earned, {} of {} spent.|r'),
('mod-temucraft-legacy', 3, ' per rank'),
('mod-temucraft-legacy', 4, 'Challenges done'),
('mod-temucraft-legacy', 5, 'Legacy rewards'),
('mod-temucraft-legacy', 6, 'Take back all my points (free)'),
('mod-temucraft-legacy', 7, 'Take back every Legacy point this character spent?'),
('mod-temucraft-legacy', 8, '|cff8a4b00{}: rank {} of {}.|r'),
('mod-temucraft-legacy', 9, 'Spend a point: {}'),
('mod-temucraft-legacy', 10, 'This perk is at its highest rank.'),
('mod-temucraft-legacy', 11, 'No Legacy points left to spend.'),
('mod-temucraft-legacy', 12, 'Back'),
('mod-temucraft-legacy', 13, 'None yet: level a class to 30, or a profession to 150.'),
('mod-temucraft-legacy', 14, '... and {} more.'),
('mod-temucraft-legacy', 15, '{} points: {} (taken)'),
('mod-temucraft-legacy', 16, '{} points: take {}'),
('mod-temucraft-legacy', 17, '{} points: {}'),
('mod-temucraft-legacy', 18, '|cffff4c4cYour bags are full.|r'),
('mod-temucraft-legacy', 19, 'The Legacy system is off on this server.');
