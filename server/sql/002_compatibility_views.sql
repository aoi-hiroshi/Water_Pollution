-- Fresh-install step 3/3. Keep the current C++ API compatible while storage is
-- unified in water_samples. These views are intentionally read-only to the app.

CREATE OR REPLACE VIEW train_data AS
SELECT
    sample_id AS id,
    temperature, ph, cod, nh3n, tp, water_level, orp,
    conductivity, dissolved_oxygen, turbidity, company_id
FROM water_samples
WHERE data_split = 'train';

CREATE OR REPLACE VIEW test_data AS
SELECT
    sample_id AS id,
    temperature, ph, cod, nh3n, tp, water_level, orp,
    conductivity, dissolved_oxygen, turbidity, company_id
FROM water_samples
WHERE data_split = 'test';
