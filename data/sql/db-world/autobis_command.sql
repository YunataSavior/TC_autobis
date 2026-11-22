-- Help text for the .autobis command (security 2 = SEC_GAMEMASTER).
-- Applied automatically by the worldserver DB updater when this repo is installed as modules/mod-autobis.
DELETE FROM `command` WHERE `name` = 'autobis';
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('autobis', 2, 'Syntax: .autobis\nGive yourself the best possible gear at your current level.');
