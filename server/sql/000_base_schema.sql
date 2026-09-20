-- Fresh-install step 1/3.
-- Select the application database first, for example:
--   CREATE DATABASE IF NOT EXISTS water_quality_system
--     CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
--   USE water_quality_system;
--
-- Raw observations for classification and forecast share one table. A row is
-- immutable after import; cleaning output belongs in processed_samples.
CREATE TABLE IF NOT EXISTS company_info (
    company_id BIGINT NOT NULL PRIMARY KEY,
    company_name VARCHAR(128) NOT NULL,
    company_code VARCHAR(64) NOT NULL,
    task_type VARCHAR(32) NOT NULL,
    sampling_interval_seconds INT UNSIGNED NOT NULL,
    location VARCHAR(255) NULL,
    description TEXT NULL,
    status VARCHAR(16) NOT NULL DEFAULT 'enabled',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_company_code(company_code),
    CONSTRAINT chk_company_task
        CHECK(task_type IN ('classification','forecast')),
    CONSTRAINT chk_company_interval CHECK(sampling_interval_seconds > 0),
    CONSTRAINT chk_company_status CHECK(status IN ('enabled','disabled'))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS water_samples (
    sample_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    company_id BIGINT NOT NULL,
    data_split ENUM('train','validation','test') NOT NULL,
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
    turbidity DOUBLE NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_water_sample_company FOREIGN KEY(company_id)
        REFERENCES company_info(company_id),
    UNIQUE KEY uk_water_sample_company(sample_id,company_id),
    UNIQUE KEY uk_water_sample_order(company_id,data_split,sample_index),
    UNIQUE KEY uk_water_sample_time(company_id,sampled_at),
    UNIQUE KEY uk_water_sample_source(company_id,data_split,source_record_id),
    INDEX idx_water_sample_scope(data_split,company_id,sample_id),
    INDEX idx_water_sample_time(company_id,sampled_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
