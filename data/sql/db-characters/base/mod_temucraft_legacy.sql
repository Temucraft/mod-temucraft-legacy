-- mod-temucraft-legacy: characters tables
CREATE TABLE IF NOT EXISTS `mod_temucraft_legacy_challenge` (
    `account`   INT UNSIGNED NOT NULL,
    `challenge` INT UNSIGNED NOT NULL COMMENT 'class*100+step, or 10000+skill*10+step',
    `time`      INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`account`, `challenge`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_temucraft_legacy_perk` (
    `guid` INT UNSIGNED NOT NULL,
    `perk` TINYINT UNSIGNED NOT NULL,
    `rank` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`, `perk`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_temucraft_legacy_reward` (
    `account` INT UNSIGNED NOT NULL,
    `points`  INT UNSIGNED NOT NULL,
    PRIMARY KEY (`account`, `points`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
