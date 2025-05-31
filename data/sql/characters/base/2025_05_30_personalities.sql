-- Drop the existing table if it exists
DROP TABLE IF EXISTS `mod_ollama_chat_personality`;

-- Create the new table with BIGINT guid and VARCHAR personality
CREATE TABLE IF NOT EXISTS `mod_ollama_chat_personality` (
  `guid` BIGINT NOT NULL,
  `personality` VARCHAR(64) NOT NULL,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;