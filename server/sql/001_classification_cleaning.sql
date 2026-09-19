-- Run against WATER_DB_NAME before saving versions. No raw table is changed.
-- The application needs SELECT + INSERT on these tables (no DDL permissions).
CREATE TABLE IF NOT EXISTS classification_cleaning_runs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    company_id BIGINT NOT NULL,
    dataset VARCHAR(32) NOT NULL,
    operation VARCHAR(32) NOT NULL,
    rule VARCHAR(96) NOT NULL,
    parent_run_id BIGINT UNSIGNED NULL,
    window_size INT UNSIGNED NOT NULL,
    z_threshold DOUBLE NOT NULL,
    sample_count BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_classification_run_scope(company_id,dataset,created_at),
    CONSTRAINT fk_classification_parent FOREIGN KEY(parent_run_id)
        REFERENCES classification_cleaning_runs(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS classification_cleaned_samples (
    run_id BIGINT UNSIGNED NOT NULL,
    source_sample_id BIGINT NOT NULL,
    company_id BIGINT NOT NULL,
    temperature DOUBLE NULL, ph DOUBLE NULL, cod DOUBLE NULL,
    nh3n DOUBLE NULL, tp DOUBLE NULL, water_level DOUBLE NULL, orp DOUBLE NULL,
    conductivity DOUBLE NULL, dissolved_oxygen DOUBLE NULL, turbidity DOUBLE NULL,
    original_temperature DOUBLE NULL, original_ph DOUBLE NULL, original_cod DOUBLE NULL,
    original_nh3n DOUBLE NULL, original_tp DOUBLE NULL, original_water_level DOUBLE NULL,
    original_orp DOUBLE NULL, original_conductivity DOUBLE NULL,
    original_dissolved_oxygen DOUBLE NULL, original_turbidity DOUBLE NULL,
    PRIMARY KEY(run_id,source_sample_id),
    CONSTRAINT fk_classification_samples FOREIGN KEY(run_id)
        REFERENCES classification_cleaning_runs(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
