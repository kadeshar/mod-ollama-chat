DROP TABLE IF EXISTS `mod_ollama_chat_personality`;
CREATE TABLE IF NOT EXISTS `mod_ollama_chat_personality` (
  `guid` int NOT NULL,
  `personality` int NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
