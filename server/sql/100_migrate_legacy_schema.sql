-- One-time migration from the original train_data/test_data and
-- classification_cleaning_* tables. Back up the database before running.
-- This script is intentionally not idempotent.
--
-- Required order for an existing database:
--   1. 000_base_schema.sql
--   2. 001_processing_model_schema.sql
--   3. this file
-- Do not run 002_compatibility_views.sql before this migration.

ALTER TABLE company_info
    ADD COLUMN sampling_interval_seconds INT UNSIGNED NULL,
    ADD COLUMN status VARCHAR(16) NOT NULL DEFAULT 'enabled';

UPDATE company_info
SET task_type = 'classification', sampling_interval_seconds = 7200
WHERE company_id BETWEEN 1 AND 6;

UPDATE company_info
SET task_type = 'forecast', sampling_interval_seconds = 60
WHERE company_id = 7;

UPDATE company_info
SET created_at = CURRENT_TIMESTAMP
WHERE created_at IS NULL;

ALTER TABLE company_info
    MODIFY task_type VARCHAR(32) NOT NULL,
    MODIFY sampling_interval_seconds INT UNSIGNED NOT NULL,
    MODIFY created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    ADD UNIQUE KEY uk_company_code(company_code),
    ADD CONSTRAINT chk_company_task
        CHECK(task_type IN ('classification','forecast')),
    ADD CONSTRAINT chk_company_interval
        CHECK(sampling_interval_seconds > 0),
    ADD CONSTRAINT chk_company_status
        CHECK(status IN ('enabled','disabled'));

INSERT INTO water_samples(
    company_id,data_split,sample_index,source_record_id,
    temperature,ph,cod,nh3n,tp,water_level,orp,conductivity,
    dissolved_oxygen,turbidity
)
SELECT
    company_id,'train',
    ROW_NUMBER() OVER(PARTITION BY company_id ORDER BY id),id,
    temperature,ph,cod,nh3n,tp,water_level,orp,conductivity,
    dissolved_oxygen,turbidity
FROM train_data;

INSERT INTO water_samples(
    company_id,data_split,sample_index,source_record_id,
    temperature,ph,cod,nh3n,tp,water_level,orp,conductivity,
    dissolved_oxygen,turbidity
)
SELECT
    company_id,'test',
    ROW_NUMBER() OVER(PARTITION BY company_id ORDER BY id),id,
    temperature,ph,cod,nh3n,tp,water_level,orp,conductivity,
    dissolved_oxygen,turbidity
FROM test_data;

INSERT INTO processing_runs(
    run_id,task_type,company_id,data_split,operation,algorithm_name,
    parent_run_id,window_size,z_threshold,sample_count,status,
    started_at,completed_at
)
SELECT
    id,'classification',company_id,
    CASE dataset WHEN 'train_data' THEN 'train' ELSE 'test' END,
    operation,rule,NULL,window_size,z_threshold,sample_count,
    'completed',created_at,created_at
FROM classification_cleaning_runs
ORDER BY id;

UPDATE processing_runs target
JOIN classification_cleaning_runs legacy ON legacy.id = target.run_id
SET target.parent_run_id = legacy.parent_run_id
WHERE legacy.parent_run_id IS NOT NULL;

INSERT INTO processed_samples(
    run_id,source_sample_id,company_id,
    temperature,ph,cod,nh3n,tp,water_level,orp,conductivity,
    dissolved_oxygen,turbidity,changed_mask
)
SELECT
    legacy_sample.run_id,raw_sample.sample_id,legacy_sample.company_id,
    legacy_sample.temperature,legacy_sample.ph,legacy_sample.cod,
    legacy_sample.nh3n,legacy_sample.tp,legacy_sample.water_level,
    legacy_sample.orp,legacy_sample.conductivity,
    legacy_sample.dissolved_oxygen,legacy_sample.turbidity,
    (NOT (legacy_sample.temperature <=> legacy_sample.original_temperature)) * 1
      + (NOT (legacy_sample.ph <=> legacy_sample.original_ph)) * 2
      + (NOT (legacy_sample.cod <=> legacy_sample.original_cod)) * 4
      + (NOT (legacy_sample.nh3n <=> legacy_sample.original_nh3n)) * 8
      + (NOT (legacy_sample.tp <=> legacy_sample.original_tp)) * 16
      + (NOT (legacy_sample.water_level <=> legacy_sample.original_water_level)) * 32
      + (NOT (legacy_sample.orp <=> legacy_sample.original_orp)) * 64
      + (NOT (legacy_sample.conductivity <=> legacy_sample.original_conductivity)) * 128
      + (NOT (legacy_sample.dissolved_oxygen <=> legacy_sample.original_dissolved_oxygen)) * 256
      + (NOT (legacy_sample.turbidity <=> legacy_sample.original_turbidity)) * 512
FROM classification_cleaned_samples legacy_sample
JOIN classification_cleaning_runs legacy_run
  ON legacy_run.id = legacy_sample.run_id
JOIN water_samples raw_sample
  ON raw_sample.company_id = legacy_sample.company_id
 AND raw_sample.data_split =
     CASE legacy_run.dataset WHEN 'train_data' THEN 'train' ELSE 'test' END
 AND raw_sample.source_record_id = legacy_sample.source_sample_id;

UPDATE processing_runs run
LEFT JOIN (
    SELECT run_id, COUNT(*) AS changed_rows
    FROM processed_samples
    WHERE changed_mask <> 0
    GROUP BY run_id
) counts ON counts.run_id = run.run_id
SET run.changed_count = COALESCE(counts.changed_rows,0)
WHERE run.task_type = 'classification';

RENAME TABLE
    train_data TO legacy_train_data,
    test_data TO legacy_test_data,
    classification_cleaned_samples TO legacy_classification_cleaned_samples,
    classification_cleaning_runs TO legacy_classification_cleaning_runs;

CREATE OR REPLACE VIEW train_data AS
SELECT
    sample_id AS id,
    temperature,ph,cod,nh3n,tp,water_level,orp,conductivity,
    dissolved_oxygen,turbidity,company_id
FROM water_samples
WHERE data_split = 'train';

CREATE OR REPLACE VIEW test_data AS
SELECT
    sample_id AS id,
    temperature,ph,cod,nh3n,tp,water_level,orp,conductivity,
    dissolved_oxygen,turbidity,company_id
FROM water_samples
WHERE data_split = 'test';

SELECT 'raw samples' AS item, COUNT(*) AS row_count FROM water_samples
UNION ALL
SELECT 'processing runs', COUNT(*) FROM processing_runs
UNION ALL
SELECT 'processed samples', COUNT(*) FROM processed_samples;
