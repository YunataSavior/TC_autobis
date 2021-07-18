USE world;
INSERT INTO command (name, help) VALUES ("autobis", "Syntax: .autobis\nGive yourself the best possible gear at your current level.");
USE auth;
INSERT INTO rbac_permissions (id, name) VALUES (1222, "Command: autobis");
INSERT INTO rbac_linked_permissions (id, linkedId) VALUES (196, 1222);
