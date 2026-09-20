-- Water_Quality_System raw sample import.
-- Run from the repository root on Linux:
--   sudo mysql --local-infile=1 Water_Quality_System \
--     < server/sql/010_import_water_samples.sql
--
-- Input file:
--   server/data/water_sampled.csv
--
-- The CSV intentionally omits sample_id and created_at. MySQL generates them.
-- Re-running this script is safe for existing business keys: duplicate rows are
-- ignored because raw observations are immutable.
-- Use an administrator for this one-time import. The Muduo runtime account
-- Aoi_@localhost should keep only the permissions needed by the service.

USE Water_Quality_System;

SET NAMES utf8mb4;

-- Seed the seven companies only when their primary keys are absent. Existing
-- company rows are preserved, including their names and descriptions.
INSERT IGNORE INTO company_info (
    company_id,
    company_name,
    company_code,
    task_type,
    location,
    description
) VALUES
    (1, '海东造船厂', 'HD_SHIP', 'trace', '台州', '溯源企业'),
    (2, '台州市椒江星明印染厂', 'XM_PRINT', 'trace', '台州', '溯源企业'),
    (3, '台州市前进化工有限公司', 'QJ_CHEM', 'trace', '台州', '溯源企业'),
    (4, '台州新农科技有限公司', 'XN_TECH', 'trace', '台州', '溯源企业'),
    (5, '浙江海正药业股份有限公司', 'HZ_WS', 'trace', '台州', '溯源企业'),
    (6, '浙江九洲药业股份有限公司', 'JZ_PHARMA', 'trace', '台州', '溯源企业'),
    (7, '浙江海正药业股份有限公司', 'HZ_YT', 'forecast', '台州', '预测企业');

DROP TEMPORARY TABLE IF EXISTS water_samples_import;

CREATE TEMPORARY TABLE water_samples_import (
    company_id BIGINT NOT NULL,
    data_split VARCHAR(16) NOT NULL,
    sample_index BIGINT UNSIGNED NOT NULL,
    sampled_at DATETIME(6) NULL,
    source_record_id BIGINT NULL,
    temperature DOUBLE NULL,
    ph DOUBLE NULL,
    cod DOUBLE NULL,
    nh3n DOUBLE NULL,
    tp DOUBLE NULL,
    water_level DOUBLE NULL,
    orp DOUBLE NULL,
    conductivity DOUBLE NULL,
    dissolved_oxygen DOUBLE NULL,
    turbidity DOUBLE NULL
) ENGINE=InnoDB;

LOAD DATA LOCAL INFILE 'server/data/water_sampled.csv'
INTO TABLE water_samples_import
CHARACTER SET utf8mb4
FIELDS TERMINATED BY ','
OPTIONALLY ENCLOSED BY '"'
LINES TERMINATED BY '\n'
IGNORE 1 LINES
(
    @company_id,
    @data_split,
    @sample_index,
    @sampled_at,
    @source_record_id,
    @temperature,
    @ph,
    @cod,
    @nh3n,
    @tp,
    @water_level,
    @orp,
    @conductivity,
    @dissolved_oxygen,
    @turbidity
)
SET
    company_id = CAST(@company_id AS UNSIGNED),
    data_split = @data_split,
    sample_index = CAST(@sample_index AS UNSIGNED),
    sampled_at = NULLIF(@sampled_at, ''),
    source_record_id = CAST(@source_record_id AS SIGNED),
    temperature = NULLIF(@temperature, ''),
    ph = NULLIF(@ph, ''),
    cod = NULLIF(@cod, ''),
    nh3n = NULLIF(@nh3n, ''),
    tp = NULLIF(@tp, ''),
    water_level = NULLIF(@water_level, ''),
    orp = NULLIF(@orp, ''),
    conductivity = NULLIF(@conductivity, ''),
    dissolved_oxygen = NULLIF(@dissolved_oxygen, ''),
    turbidity = NULLIF(@turbidity, '');

SELECT COUNT(*) INTO @staged_rows FROM water_samples_import;
SELECT COUNT(*) INTO @before_rows FROM water_samples;

START TRANSACTION;

INSERT IGNORE INTO water_samples (
    company_id,
    data_split,
    sample_index,
    sampled_at,
    source_record_id,
    temperature,
    ph,
    cod,
    nh3n,
    tp,
    water_level,
    orp,
    conductivity,
    dissolved_oxygen,
    turbidity
)
SELECT
    company_id,
    data_split,
    sample_index,
    sampled_at,
    source_record_id,
    temperature,
    ph,
    cod,
    nh3n,
    tp,
    water_level,
    orp,
    conductivity,
    dissolved_oxygen,
    turbidity
FROM water_samples_import;

SET @inserted_rows = ROW_COUNT();

COMMIT;

SELECT COUNT(*) INTO @after_rows FROM water_samples;

SELECT
    @staged_rows AS staged_rows,
    @inserted_rows AS inserted_rows,
    @before_rows AS rows_before_import,
    @after_rows AS rows_after_import;

SELECT
    company_id,
    data_split,
    COUNT(*) AS row_count,
    MIN(sample_index) AS first_sample_index,
    MAX(sample_index) AS last_sample_index,
    MIN(sampled_at) AS first_sampled_at,
    MAX(sampled_at) AS last_sampled_at
FROM water_samples
WHERE company_id BETWEEN 1 AND 7
GROUP BY company_id, data_split
ORDER BY company_id, FIELD(data_split, 'train', 'validation', 'test');

DROP TEMPORARY TABLE IF EXISTS water_samples_import;
