/* BOOK-1 reference: the geometry code of the "Gastro Institute 3D" page (the model the institute.map records were exported
   from and checked against), without the book's text. It builds every storey's shell, room furniture, lifts, roof and site
   into triangle soups (MB: p = xyz per vertex, h = colour per triangle). Run it under Node like tools/book/model_furniture.js
   does, or read it as the specification for MAP-2's C scene builder: same dimensions, same colours (COL), same furniture
   builders (PROP, furnish, extras), same door / window / wall-piece rules (buildRoomShell, wall). Not shipped, not built. */
var ROOMS=[{"f": 0, "id": "g_reflux1", "type": "exam", "x0": -26, "z0": -18, "x1": -17, "z1": -11, "label": "Cabinet reflux 1", "sign": "Cabinet reflux 1", "door": "S", "dc": -18.0, "topic": "brge_epi", "slot": "desk"}, {"f": 0, "id": "g_reflux2", "type": "exam", "x0": -17, "z0": -18, "x1": -11, "z1": -13, "label": "Cabinet reflux 2", "sign": "Cabinet reflux 2", "door": "S", "dc": -14.0, "topic": "brge_clinic", "slot": "desk"}, {"f": 0, "id": "g_dispepsie", "type": "exam", "x0": -11, "z0": -18, "x1": -5, "z1": -11, "label": "Cabinet dispepsie", "sign": "Cabinet dispepsie", "door": "S", "dc": -8.0, "topic": "ulcer_clinic", "slot": "desk"}, {"f": 0, "id": "g_gastrite", "type": "exam", "x0": -5, "z0": -18, "x1": 1, "z1": -13, "label": "Cabinet gastrite", "sign": "Cabinet gastrite", "door": "S", "dc": -2.0, "topic": "gastrita", "slot": "desk"}, {"f": 0, "id": "g_fiziologie", "type": "lab", "x0": 1, "z0": -18, "x1": 15, "z1": -11, "label": "Fiziologie digestivă", "sign": "Laborator fiziologie digestivă · pH-metrie & manometrie", "door": "S", "dc": 8.0, "topic": "brge_dx", "topic2": "acalazia", "slot": "screen"}, {"f": 0, "id": "g_triaj", "type": "triage", "x0": 15, "z0": -18, "x1": 26, "z1": -11, "label": "Triaj", "sign": "Triaj", "door": "S", "dc": 17.0}, {"f": 0, "id": "g_amfiteatru", "type": "meeting", "x0": -26, "z0": 11, "x1": -13, "z1": 18, "label": "Amfiteatru", "sign": "Amfiteatru", "door": "N", "dc": -16.0, "topic": "intro", "slot": "board"}, {"f": 0, "id": "g_farmacie", "type": "pharmacy", "x0": -13, "z0": 11, "x1": -5, "z1": 18, "label": "Farmacie ambulatoriu", "sign": "Farmacie ambulatoriu", "door": "N", "dc": -9.0, "topic": "brge_tx", "slot": "counter"}, {"f": 0, "id": "g_intrare", "type": "waiting", "x0": -5, "z0": 11, "x1": 5, "z1": 18, "label": "Hol de intrare", "sign": "Hol de intrare", "door": "N", "dc": 0.0, "open_n": true, "entrance": true}, {"f": 0, "id": "g_tranzit", "type": "exam", "x0": 5, "z0": 13, "x1": 12, "z1": 18, "label": "Tulburări de tranzit", "sign": "Cabinet tulburări de tranzit", "door": "N", "dc": 8.5, "topic": "constipatia", "slot": "desk"}, {"f": 0, "id": "g_garda", "type": "nurse_station", "x0": 12, "z0": 11, "x1": 26, "z1": 18, "label": "Stație Gardă", "sign": "Stație Gardă", "door": "N", "dc": 15.5, "role": "garda", "slot": "desk", "ambulance": true}, {"f": 0, "id": "g_stoma", "type": "exam", "x0": -26, "z0": -11, "x1": -19, "z1": -5, "label": "Cabinet stomatologie", "sign": "Cabinet stomatologie", "door": "E", "dc": -8.0, "topic": "oral_gingii", "slot": "desk"}, {"f": 0, "id": "g_salivare", "type": "exam", "x0": -26, "z0": -5, "x1": -19, "z1": 1, "label": "Cabinet glande salivare", "sign": "Cabinet glande salivare", "door": "E", "dc": -2.0, "topic": "oral_salivare", "slot": "desk"}, {"f": 0, "id": "g_asteptare", "type": "waiting", "x0": -26, "z0": 1, "x1": -19, "z1": 11, "label": "Sală de așteptare", "sign": "Sală de așteptare", "door": "E", "dc": 6.0}, {"f": 0, "id": "g_bay1", "type": "treatment_bay", "x0": 19, "z0": -11, "x1": 26, "z1": -7.33, "label": "Box 1", "sign": "Box gardă 1", "door": "W", "dc": -9.16, "role": "garda_case", "slot": "bed"}, {"f": 0, "id": "g_bay2", "type": "treatment_bay", "x0": 19, "z0": -7.33, "x1": 26, "z1": -3.67, "label": "Box 2", "sign": "Box gardă 2", "door": "W", "dc": -5.5, "role": "garda_case", "slot": "bed"}, {"f": 0, "id": "g_bay3", "type": "treatment_bay", "x0": 19, "z0": -3.67, "x1": 26, "z1": -0.0, "label": "Box 3", "sign": "Box gardă 3", "door": "W", "dc": -1.83, "role": "garda_case", "slot": "bed"}, {"f": 0, "id": "g_bay4", "type": "treatment_bay", "x0": 19, "z0": -0.0, "x1": 26, "z1": 3.67, "label": "Box 4", "sign": "Box gardă 4", "door": "W", "dc": 1.83, "role": "garda_case", "slot": "bed"}, {"f": 0, "id": "g_bay5", "type": "treatment_bay", "x0": 19, "z0": 3.67, "x1": 26, "z1": 7.33, "label": "Box 5", "sign": "Box gardă 5", "door": "W", "dc": 5.5, "role": "garda_case", "slot": "bed"}, {"f": 0, "id": "g_bay6", "type": "treatment_bay", "x0": 19, "z0": 7.33, "x1": 26, "z1": 11.0, "label": "Box 6", "sign": "Box gardă 6", "door": "W", "dc": 9.16, "role": "garda_case", "slot": "bed"}, {"f": 0, "id": "g_lobby", "type": "reception", "x0": -9, "z0": -8, "x1": 9, "z1": 8, "label": "Atrium · Recepție & Panou", "sign": "Atrium · Recepție & Panou de orientare", "door": "S", "dc": 0, "topic": "cuprins", "role": "board", "slot": "board", "open": true}, {"f": 1, "id": "e_microbio", "type": "lab", "x0": -26, "z0": -18, "x1": -16, "z1": -11, "label": "Microbiologie", "sign": "Laborator microbiologie", "door": "S", "dc": -17.5, "topic": "hp_bact", "slot": "bench"}, {"f": 1, "id": "e_anapat", "type": "lab", "x0": -16, "z0": -18, "x1": -9, "z1": -11, "label": "Anatomie patologică", "sign": "Anatomie patologică gastrică", "door": "S", "dc": -12.5, "topic": "hp_consec", "slot": "bench"}, {"f": 1, "id": "e_respirator", "type": "lab", "x0": -9, "z0": -18, "x1": -2, "z1": -13, "label": "Teste respiratorii", "sign": "Laborator teste respiratorii & serologie", "door": "S", "dc": -5.5, "topic": "hp_dx_noninv", "slot": "bench"}, {"f": 1, "id": "e_histo", "type": "lab", "x0": -2, "z0": -18, "x1": 10, "z1": -11, "label": "Histopatologie", "sign": "Laborator histopatologie", "door": "S", "dc": 4.0, "topic": "bii_histo", "topic2": "colita_micro", "slot": "bench"}, {"f": 1, "id": "e_analize", "type": "lab", "x0": 10, "z0": -18, "x1": 17, "z1": -11, "label": "Laborator analize", "sign": "Laborator analize medicale", "door": "S", "dc": 13.5, "topic": "bii_lab", "slot": "bench"}, {"f": 1, "id": "e_vestiar", "type": "store", "x0": 17, "z0": -18, "x1": 26, "z1": -11, "label": "Vestiare personal", "sign": "Vestiare personal", "door": "S", "dc": 18.0, "locker": true}, {"f": 1, "id": "e_primire", "type": "waiting", "x0": -26, "z0": 11, "x1": -14, "z1": 18, "label": "Primire & pregătire", "sign": "Primire & pregătire pacienți", "door": "N", "dc": -16.5}, {"f": 1, "id": "e_endo1", "type": "exam", "x0": -14, "z0": 11, "x1": -7.5, "z1": 18, "label": "Endoscopie 1", "sign": "Sală endoscopie 1", "door": "N", "dc": -10.75, "topic": "esofagita", "slot": "scope"}, {"f": 1, "id": "e_endo2", "type": "exam", "x0": -7.5, "z0": 11, "x1": -1.0, "z1": 18, "label": "Endoscopie 2", "sign": "Sală endoscopie 2 · înaltă definiție", "door": "N", "dc": -4.25, "topic": "barrett_dx", "slot": "scope"}, {"f": 1, "id": "e_decontam", "type": "sterile", "x0": -1.0, "z0": 11, "x1": 3.0, "z1": 18, "label": "Decontaminare & depozit", "sign": "Decontaminare endoscoape", "door": "N", "dc": 1.0}, {"f": 1, "id": "e_endoter", "type": "exam", "x0": 3.0, "z0": 11, "x1": 11.0, "z1": 18, "label": "Endoscopie terapeutică", "sign": "Sală endoscopie terapeutică", "door": "N", "dc": 7.0, "topic": "barrett_tx", "slot": "scope"}, {"f": 1, "id": "e_endo3", "type": "exam", "x0": 11.0, "z0": 11, "x1": 17.0, "z1": 18, "label": "Gastroscopie", "sign": "Sală gastroscopie", "door": "N", "dc": 14.0, "topic": "hp_dx_inv", "slot": "scope"}, {"f": 1, "id": "e_recuperare", "type": "recovery", "x0": 17.0, "z0": 11, "x1": 26.0, "z1": 18, "label": "Recuperare", "sign": "Recuperare post-procedură", "door": "N", "dc": 18.0}, {"f": 1, "id": "e_farmclin", "type": "pharmacy", "x0": -26, "z0": -11, "x1": -19, "z1": 2, "label": "Farmacie clinică", "sign": "Farmacie clinică", "door": "E", "dc": -4.5, "topic": "hp_tx", "slot": "counter"}, {"f": 1, "id": "e_functional", "type": "exam", "x0": -26, "z0": 2, "x1": -21, "z1": 11, "label": "Tulburări funcționale", "sign": "Cabinet tulburări funcționale", "door": "E", "dc": 6.5, "topic": "esofag_functional", "slot": "desk"}, {"f": 1, "id": "e_colono", "type": "exam", "x0": 19, "z0": -11, "x1": 26, "z1": -2, "label": "Colonoscopie", "sign": "Sală colonoscopie & capsulă", "door": "W", "dc": -6.5, "topic": "bii_endo", "slot": "scope"}, {"f": 1, "id": "e_radiologie", "type": "imaging", "x0": 19, "z0": -2, "x1": 26, "z1": 11, "label": "Radiologie & medicină nucleară", "sign": "Radiologie, imagistică & medicină nucleară", "door": "W", "dc": 4.5, "topic": "bii_imag", "topic2": "gastropareza", "slot": "screen"}, {"f": 2, "id": "w_epi", "type": "office", "x0": -26, "z0": -18, "x1": -17, "z1": -11, "label": "Epidemiologie", "sign": "Birou epidemiologie & registru BII", "door": "S", "dc": -18.0, "topic": "bii_epi", "slot": "desk"}, {"f": 2, "id": "w_genetica", "type": "lab", "x0": -17, "z0": -18, "x1": -5, "z1": -11, "label": "Cercetare genetică", "sign": "Laborator cercetare genetică", "door": "S", "dc": -11.0, "topic": "bii_genetica", "slot": "bench"}, {"f": 2, "id": "w_imuno", "type": "lab", "x0": -5, "z0": -18, "x1": 5, "z1": -11, "label": "Microbiotă & imunologie", "sign": "Laborator microbiotă & imunologie", "door": "S", "dc": 0.0, "topic": "bii_microbiota", "slot": "bench"}, {"f": 2, "id": "w_interdis", "type": "exam", "x0": 5, "z0": -18, "x1": 13, "z1": -13, "label": "Consult interdisciplinar", "sign": "Cabinet consult interdisciplinar", "door": "S", "dc": 9.0, "topic": "bii_extra", "slot": "desk"}, {"f": 2, "id": "w_monitor", "type": "exam", "x0": 13, "z0": -18, "x1": 26, "z1": -11, "label": "Monitorizare & screening", "sign": "Cabinet monitorizare & screening", "door": "S", "dc": 16.0, "topic": "bii_prognostic", "slot": "desk"}, {"f": 2, "id": "w_g1", "type": "ward_room", "x0": -26, "z0": 11, "x1": -15, "z1": 18, "label": "Salon G1", "sign": "Salon G1", "door": "N", "dc": -17.0, "topic": "ulcer_compl", "slot": "bed"}, {"f": 2, "id": "w_g2", "type": "ward_room", "x0": -15, "z0": 11, "x1": -9.5, "z1": 18, "label": "Salon G2", "sign": "Salon G2", "door": "N", "dc": -12.25, "topic": "ains", "slot": "bed"}, {"f": 2, "id": "w_g3", "type": "ward_room", "x0": -9.5, "z0": 11, "x1": -4.0, "z1": 18, "label": "Salon G3", "sign": "Salon G3", "door": "N", "dc": -6.75, "topic": "hp_alte", "slot": "bed"}, {"f": 2, "id": "w_statie", "type": "nurse_station", "x0": -4.0, "z0": 11, "x1": 4.0, "z1": 18, "label": "Stație asistente", "sign": "Stație Gastroenterologie", "door": "N", "dc": 0.0, "role": "round", "slot": "post"}, {"f": 2, "id": "w_b1", "type": "ward_room", "x0": 4.0, "z0": 11, "x1": 9.5, "z1": 18, "label": "Salon B1", "sign": "Salon B1", "door": "N", "dc": 6.75, "topic": "bc_clinic", "slot": "bed"}, {"f": 2, "id": "w_b2", "type": "ward_room", "x0": 9.5, "z0": 11, "x1": 15.0, "z1": 18, "label": "Salon B2", "sign": "Salon B2", "door": "N", "dc": 12.25, "topic": "bc_inductie", "slot": "bed"}, {"f": 2, "id": "w_b3", "type": "ward_room", "x0": 15.0, "z0": 11, "x1": 26.0, "z1": 18, "label": "Salon B3", "sign": "Salon B3", "door": "N", "dc": 17.0, "topic": "cu_clinic", "slot": "bed"}, {"f": 2, "id": "w_complicatii", "type": "ward_room", "x0": -26, "z0": -11, "x1": -19, "z1": -3, "label": "Sala de complicații", "sign": "Sala de complicații", "door": "E", "dc": -7.0, "role": "complication", "slot": "bed"}, {"f": 2, "id": "w_perfuzii", "type": "treatment_bay", "x0": -26, "z0": -3, "x1": -19, "z1": 11, "label": "Centru de perfuzii", "sign": "Centru de perfuzii · terapii biologice", "door": "E", "dc": 4.0, "topic": "bc_mentinere", "slot": "bed"}, {"f": 2, "id": "w_b4", "type": "ward_room", "x0": 19, "z0": -11, "x1": 26, "z1": -4, "label": "Salon B4", "sign": "Salon B4", "door": "W", "dc": -7.5, "topic": "cu_tx", "slot": "bed"}, {"f": 2, "id": "w_intermediar", "type": "icu_bay", "x0": 19, "z0": -4, "x1": 26, "z1": 11, "label": "Terapie intermediară", "sign": "Terapie intermediară", "door": "W", "dc": 3.5, "topic": "cu_sever", "slot": "bed"}, {"f": 3, "id": "o_genetica", "type": "exam", "x0": -26, "z0": -18, "x1": -17, "z1": -11, "label": "Consiliere genetică & tumori", "sign": "Cabinet consiliere genetică & tumori", "door": "S", "dc": -18.0, "topic": "intestin_tumori", "slot": "desk"}, {"f": 3, "id": "o_onco", "type": "exam", "x0": -17, "z0": -18, "x1": -11, "z1": -13, "label": "Oncologie digestivă", "sign": "Cabinet oncologie digestivă", "door": "S", "dc": -14.0, "topic": "esofag_tumori", "slot": "desk"}, {"f": 3, "id": "o_sef", "type": "office", "x0": -11, "z0": -18, "x1": -4, "z1": -11, "label": "Birou medic șef", "sign": "Birou medic șef", "door": "S", "dc": -7.5, "role": "chief", "slot": "desk"}, {"f": 3, "id": "o_comisie", "type": "meeting", "x0": -4, "z0": -18, "x1": 8, "z1": -11, "label": "Sala Comisiei", "sign": "Sala Comisiei multidisciplinare", "door": "S", "dc": 2.0, "role": "boss", "slot": "table"}, {"f": 3, "id": "o_liga", "type": "meeting", "x0": 8, "z0": -18, "x1": 17, "z1": -11, "label": "Sala Ligii", "sign": "Sala Ligii", "door": "S", "dc": 12.5, "role": "league", "slot": "board"}, {"f": 3, "id": "o_birou", "type": "office", "x0": 17, "z0": -18, "x1": 26, "z1": -11, "label": "Birou personal", "sign": "Birou personal", "door": "S", "dc": 18.0, "role": "office", "slot": "desk"}, {"f": 3, "id": "o_so1", "type": "or", "x0": -26, "z0": 11, "x1": -14, "z1": 18, "label": "Sală operație 1", "sign": "Sală operație 1 · laparoscopie", "door": "N", "dc": -16.5, "topic": "brge_chir", "slot": "or_table"}, {"f": 3, "id": "o_so2", "type": "or", "x0": -14, "z0": 11, "x1": -6, "z1": 18, "label": "Sală operație 2", "sign": "Sală operație 2 · chirurgie gastrică", "door": "N", "dc": -10.0, "topic": "ulcer_chir", "slot": "or_table"}, {"f": 3, "id": "o_so3", "type": "or", "x0": -6, "z0": 11, "x1": 2, "z1": 18, "label": "Sală operație 3", "sign": "Sală operație 3 · chirurgie colorectală", "door": "N", "dc": -2.0, "topic": "bc_chir", "slot": "or_table"}, {"f": 3, "id": "o_so4", "type": "or", "x0": 2, "z0": 11, "x1": 10, "z1": 18, "label": "Sală operație 4", "sign": "Sală operație 4 · colectomie & rezervor", "door": "N", "dc": 6.0, "topic": "cu_chir", "slot": "or_table"}, {"f": 3, "id": "o_biblioteca", "type": "meeting", "x0": 10, "z0": 11, "x1": 17, "z1": 18, "label": "Bibliotecă", "sign": "Bibliotecă", "door": "N", "dc": 13.5, "topic": "lecturi_esofag", "topic2": "lecturi_intestin", "topic3": "lecturi_bii", "role": "library", "slot": "reading_desk"}, {"f": 3, "id": "o_terasa", "type": "terrace", "x0": 17, "z0": 11, "x1": 26, "z1": 18, "label": "Terasă", "sign": "Terasă", "door": "N", "dc": 18.0}, {"f": 3, "id": "o_filtru", "type": "scrub", "x0": -26, "z0": -11, "x1": -19, "z1": -5, "label": "Filtru & spălător", "sign": "Filtru & spălător", "door": "E", "dc": -8.0}, {"f": 3, "id": "o_recuperare", "type": "recovery", "x0": -26, "z0": -5, "x1": -19, "z1": 11, "label": "Recuperare & stomaterapie", "sign": "Recuperare postoperatorie & stomaterapie", "door": "E", "dc": 3.0}, {"f": 3, "id": "o_examen", "type": "meeting", "x0": 19, "z0": -11, "x1": 26, "z1": 11.0, "label": "Sala de examen", "sign": "Sala de examen", "door": "W", "dc": 0.0, "role": "exam", "slot": "desk"}, {"f": 0, "id": "c0_wc", "type": "toilet", "x0": -16, "z0": -8, "x1": -9, "z1": -3, "label": "WC", "sign": "Toalete", "door": "N", "dc": -12.5}, {"f": 0, "id": "c0_scari", "type": "store", "x0": -16, "z0": -3, "x1": -9, "z1": 3, "label": "Scări", "sign": "Scări", "door": "W", "dc": 0, "stairs": true}, {"f": 0, "id": "c0_wc2", "type": "toilet", "x0": 9, "z0": -8, "x1": 16, "z1": -3, "label": "WC", "sign": "Toalete", "door": "N", "dc": 12.5}, {"f": 0, "id": "c0_depozit", "type": "store", "x0": 9, "z0": -3, "x1": 16, "z1": 3, "label": "Depozit", "sign": "Depozit", "door": "E", "dc": 0}, {"f": 1, "id": "c1_wc", "type": "toilet", "x0": -16, "z0": -8, "x1": -9, "z1": -3, "label": "WC", "sign": "Toalete", "door": "N", "dc": -12.5}, {"f": 1, "id": "c1_scari", "type": "store", "x0": -16, "z0": -3, "x1": -9, "z1": 3, "label": "Scări", "sign": "Scări", "door": "W", "dc": 0, "stairs": true}, {"f": 1, "id": "c1_wc2", "type": "toilet", "x0": 9, "z0": -8, "x1": 16, "z1": -3, "label": "WC", "sign": "Toalete", "door": "N", "dc": 12.5}, {"f": 1, "id": "c1_depozit", "type": "store", "x0": 9, "z0": -3, "x1": 16, "z1": 3, "label": "Depozit", "sign": "Depozit", "door": "E", "dc": 0}, {"f": 2, "id": "c2_wc", "type": "toilet", "x0": -16, "z0": -8, "x1": -9, "z1": -3, "label": "WC", "sign": "Toalete", "door": "N", "dc": -12.5}, {"f": 2, "id": "c2_scari", "type": "store", "x0": -16, "z0": -3, "x1": -9, "z1": 3, "label": "Scări", "sign": "Scări", "door": "W", "dc": 0, "stairs": true}, {"f": 2, "id": "c2_wc2", "type": "toilet", "x0": 9, "z0": -8, "x1": 16, "z1": -3, "label": "WC", "sign": "Toalete", "door": "N", "dc": 12.5}, {"f": 2, "id": "c2_depozit", "type": "store", "x0": 9, "z0": -3, "x1": 16, "z1": 3, "label": "Depozit", "sign": "Depozit", "door": "E", "dc": 0}, {"f": 3, "id": "c3_wc", "type": "toilet", "x0": -16, "z0": -8, "x1": -9, "z1": -3, "label": "WC", "sign": "Toalete", "door": "N", "dc": -12.5}, {"f": 3, "id": "c3_scari", "type": "store", "x0": -16, "z0": -3, "x1": -9, "z1": 3, "label": "Scări", "sign": "Scări", "door": "W", "dc": 0, "stairs": true}, {"f": 3, "id": "c3_wc2", "type": "toilet", "x0": 9, "z0": -8, "x1": 16, "z1": -3, "label": "WC", "sign": "Toalete", "door": "N", "dc": 12.5}, {"f": 3, "id": "c3_depozit", "type": "store", "x0": 9, "z0": -3, "x1": 16, "z1": 3, "label": "Depozit", "sign": "Depozit", "door": "E", "dc": 0}];
var TOPICS={"intro": {"c": 0}, "brge_epi": {"c": 1}, "brge_clinic": {"c": 1}, "brge_dx": {"c": 1}, "brge_tx": {"c": 1}, "esofagita": {"c": 1}, "barrett_dx": {"c": 1}, "barrett_tx": {"c": 1}, "brge_chir": {"c": 1}, "gastrita": {"c": 2}, "hp_bact": {"c": 2}, "hp_consec": {"c": 2}, "ulcer_clinic": {"c": 2}, "hp_dx_noninv": {"c": 2}, "hp_dx_inv": {"c": 2}, "hp_tx": {"c": 2}, "ulcer_compl": {"c": 2}, "ulcer_chir": {"c": 2}, "hp_alte": {"c": 2}, "ains": {"c": 2}, "bii_epi": {"c": 3}, "bii_genetica": {"c": 3}, "bii_microbiota": {"c": 3}, "bii_endo": {"c": 3}, "bii_histo": {"c": 3}, "bii_extra": {"c": 3}, "bc_clinic": {"c": 3}, "bii_lab": {"c": 3}, "bii_imag": {"c": 3}, "bc_inductie": {"c": 3}, "bc_mentinere": {"c": 3}, "bc_chir": {"c": 3}, "cu_clinic": {"c": 4}, "cu_tx": {"c": 4}, "cu_sever": {"c": 4}, "cu_chir": {"c": 4}, "bii_prognostic": {"c": 4}, "colita_micro": {"c": 4}, "cuprins": {"c": 0}, "oral_gingii": {"c": 1}, "oral_salivare": {"c": 1}, "acalazia": {"c": 1}, "esofag_tumori": {"c": 1}, "esofag_functional": {"c": 1}, "lecturi_esofag": {"c": 1}, "gastropareza": {"c": 2}, "intestin_tumori": {"c": 3}, "lecturi_intestin": {"c": 3}, "lecturi_bii": {"c": 4}, "constipatia": {"c": 4}}; /* chapter of each topic only: the titles are the book's and stay out of the repository */
var CHAPS=[{"n": "Prima zi", "s": "Introducere"}, {"n": "Arsura", "s": "Gură, reflux și esofag"}, {"n": "Bacteria", "s": "Stomac și duoden"}, {"n": "Inflamația", "s": "Intestin subțire, BII, Crohn"}, {"n": "Colita", "s": "Colita ulcerativă, colon și rect"}];
var FLOORS=["G · Ambulatoriu & Urgențe", "1 · Endoscopie & Laboratoare", "2 · Secții & Cercetare", "3 · Bloc operator & Academic"];
var DIM={"BX": 26, "BZ": 18, "AX": 9, "AZ": 8, "CO": [19, 11], "CI": [16, 8]};
var TOWERS=[{"x0": -16, "x1": -9, "z0": 3, "z1": 8, "lifts": [-14.1, -11.1]}, {"x0": 9, "x1": 16, "z0": 3, "z1": 8, "lifts": [12.5]}];
var CORR=[[-19, -11, 19, -8], [-19, 8, 19, 11], [-19, -8, -16, 8], [16, -8, 19, 8]];
var ALCOVES=[{"f": 0, "x0": -17, "z0": -13, "x1": -11, "z1": -11, "side": "N"}, {"f": 0, "x0": -5, "z0": -13, "x1": 1, "z1": -11, "side": "N"}, {"f": 0, "x0": 5, "z0": 11, "x1": 12, "z1": 13, "side": "S"}, {"f": 1, "x0": -9, "z0": -13, "x1": -2, "z1": -11, "side": "N"}, {"f": 1, "x0": -21, "z0": 2, "x1": -19, "z1": 11, "side": "W"}, {"f": 2, "x0": 5, "z0": -13, "x1": 13, "z1": -11, "side": "N"}, {"f": 3, "x0": -17, "z0": -13, "x1": -11, "z1": -11, "side": "N"}];
/* ---------- geometry (no rendering): metres, y up, x east, z south ---------- */
var TAU=Math.PI*2,FH=4,WH=3.7,T=.1;
var COL={wall:'f3ead8',wallx:'e9dcc2',trim:'d8c9aa',slab:'cfc8ba',corr:'e3e8ec',glass:'7fc4e8',frame:'5b6b78',door:'2f6fb7',steel:'aab4bd',dark:'2f3640',white:'f7f7f2',red:'e23b3b',
 wood:'b98a5a',wood2:'8a6238',bedframe:'c9d0d6',sheet:'ffffff',blanket:'7fc4e8',blanket2:'9fd8c8',green:'3ecf8e',scrub:'4fb3a5',screen:'1d2a36',screenOn:'6cc0ea',yellow:'ffd166',
 grass:'7cc95a',grass2:'8fd764',road:'8a929a',line:'ffffff',pave:'d9d4c8',leaf:'3f9a4a',leaf2:'57b356',trunk:'7a5230',water:'55c3e0',roof:'b9c0c7',roof2:'9aa3ab',hvac:'dfe4e8',
 f_exam:'e8ecf0',f_lab:'dfe9f2',f_ward:'e8ecf0',f_or:'cfe3ea',f_pharm:'efe6d2',f_meet:'e9dcc6',f_office:'e6dccb',f_recep:'f0e6d0',f_ed:'dde8e1',f_wc:'d5dbe0',f_terrace:'cfc9be',f_img:'d9dfe8',
 brick:'b9694a',hedge:'4a9a44',curb:'c4beb0',solar:'24406e',skin:'f0c49a',skin2:'c98d62',hair:'4a3222',hair2:'d9b36a',coat:'fbfbf6',car1:'d64545',car2:'3f7fd0',car3:'f2c230',car4:'e9edf0',car5:'3a4450',flower:'f06fa0',flower2:'ffd166',tyre:'22262c',stone:'9aa0a6',
 c0:'4d9bd6',c1:'e6a52e',c2:'2fae93',c3:'8a63d8',c4:'e0566e',role:'6b7fd0',plant:'4c9f45',pot:'c9553f',orange:'f08a24',blue:'3f7fd0',pink:'f1b3bd'};
var FLOORCOL={exam:'f_exam',lab:'f_lab',ward_room:'f_ward',icu_bay:'f_or',or:'f_or',scrub:'f_or',sterile:'f_or',recovery:'f_ward',pharmacy:'f_pharm',meeting:'f_meet',office:'f_office',reception:'f_recep',
 treatment_bay:'f_ed',nurse_station:'f_ed',toilet:'f_wc',store:'f_wc',terrace:'f_terrace',imaging:'f_img',waiting:'f_recep'};
var KINDS={tile:['f_exam','f_lab','f_ward','f_or','f_ed','f_wc','f_img','corr','f_recep','f_pharm'],wood:['f_meet','f_office','wood','wood2'],paving:['f_terrace','pave','curb'],
 plaster:['wall','wallx','trim'],brick:['brick'],asphalt:['road'],grass:['grass','grass2'],gravel:['roof','roof2'],water:['water'],hedge:['hedge','leaf','leaf2','plant']};
var KIND_OF={};Object.keys(KINDS).forEach(function(k){KINDS[k].forEach(function(c){KIND_OF[COL[c]]=k})});
function MB(){this.p=[];this.h=[]}
MB.prototype.rbox=function(cx,cy,cz,sx,sy,sz,col,yaw){var c=Math.cos(yaw),s=Math.sin(yaw),o=[[-1,-1],[1,-1],[1,1],[-1,1]],v=[],k,i,m=[cx,cy,cz];
  for(k=0;k<2;k++)for(i=0;i<4;i++){var ox=o[i][0]*sx/2,oz=o[i][1]*sz/2;v.push([cx+ox*c-oz*s,cy+(k?1:-1)*sy/2,cz+ox*s+oz*c])}
  this.quad(v[0],v[1],v[2],v[3],col,m);this.quad(v[4],v[5],v[6],v[7],col,m);for(i=0;i<4;i++){var j=(i+1)%4;this.quad(v[i],v[j],v[j+4],v[i+4],col,m)}};
MB.prototype.tri=function(a,b,c,col,ref){
  if(ref){var ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2],vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2];
    var nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
    var mx=(a[0]+b[0]+c[0])/3-ref[0],my=(a[1]+b[1]+c[1])/3-ref[1],mz=(a[2]+b[2]+c[2])/3-ref[2];
    if(nx*mx+ny*my+nz*mz<0){var t=b;b=c;c=t}}
  this.p.push(a[0],a[1],a[2],b[0],b[1],b[2],c[0],c[1],c[2]);this.h.push(col)};
MB.prototype.quad=function(a,b,c,d,col,ref){this.tri(a,b,c,col,ref);this.tri(a,c,d,col,ref)};
MB.prototype.box=function(cx,cy,cz,sx,sy,sz,col,skipBottom){var x0=cx-sx/2,x1=cx+sx/2,y0=cy-sy/2,y1=cy+sy/2,z0=cz-sz/2,z1=cz+sz/2,m=[cx,cy,cz];
  this.quad([x0,y1,z0],[x1,y1,z0],[x1,y1,z1],[x0,y1,z1],col,m);if(!skipBottom)this.quad([x0,y0,z0],[x1,y0,z0],[x1,y0,z1],[x0,y0,z1],col,m);
  this.quad([x0,y0,z0],[x1,y0,z0],[x1,y1,z0],[x0,y1,z0],col,m);this.quad([x0,y0,z1],[x1,y0,z1],[x1,y1,z1],[x0,y1,z1],col,m);
  this.quad([x0,y0,z0],[x0,y0,z1],[x0,y1,z1],[x0,y1,z0],col,m);this.quad([x1,y0,z0],[x1,y0,z1],[x1,y1,z1],[x1,y1,z0],col,m)};
/* box sitting on y: b(x,y0,z, sx,h,sz) */
MB.prototype.b=function(x,y0,z,sx,h,sz,col){this.box(x,y0+h/2,z,sx,h,sz,col,true)};
MB.prototype.cyl=function(c,r0,r1,h,n,colS,colC){var mid=[c[0],c[1]+h/2,c[2]],c1=[c[0],c[1]+h,c[2]];
  for(var i=0;i<n;i++){var a0=i/n*TAU,a1=(i+1)/n*TAU,a=[c[0]+r0*Math.cos(a0),c[1],c[2]+r0*Math.sin(a0)],b=[c[0]+r0*Math.cos(a1),c[1],c[2]+r0*Math.sin(a1)];
    if(r1>0){var d=[c[0]+r1*Math.cos(a0),c[1]+h,c[2]+r1*Math.sin(a0)],e=[c[0]+r1*Math.cos(a1),c[1]+h,c[2]+r1*Math.sin(a1)];this.quad(a,b,e,d,colS,mid);this.tri(c1,d,e,colC||colS,mid)}else this.tri(a,b,c1,colS,mid)}};
MB.prototype.count=function(){return this.h.length};

/* a wall along x (axis 'x', fixed z) or along z (axis 'z', fixed x); gaps = [{c,w,sill,top,glass}] */
function wall(mb,axis,a0,a1,fix,th,y,h,col,gaps){gaps=(gaps||[]).slice().sort(function(p,q){return p.c-q.c});var cur=a0;
  function seg(s,e,y0,hh,c,t2){if(e-s<.01||hh<.01)return;var m=(s+e)/2,l=e-s;if(axis==='x')mb.b(m,y0,fix,l,hh,t2||th,c);else mb.b(fix,y0,m,t2||th,hh,l,c)}
  gaps.forEach(function(g){var s=Math.max(a0,g.c-g.w/2),e=Math.min(a1,g.c+g.w/2);seg(cur,s,y,h,col);
    seg(s,e,y,g.sill||0,col);seg(s,e,y+g.top,h-g.top,col);if(g.glass){seg(s,e,y+g.sill,g.top-g.sill,COL.glass,.04);var mm=(s+e)/2;seg(mm-.04,mm+.04,y+g.sill,g.top-g.sill,COL.frame,th+.05);seg(s,e,y+(g.sill+g.top)/2-.03,.06,COL.frame,th+.05);
      seg(s-.08,e+.08,y+g.sill-.1,.1,COL.white,th+.22);seg(s-.08,e+.08,y+g.top,.12,COL.trim,th+.12)}cur=e});
  seg(cur,a1,y,h,col)}
var BX=DIM.BX,BZ=DIM.BZ,AX=DIM.AX,AZ=DIM.AZ,DOORS=[],DOOR_OPEN=1.25;
/* unit door leaf: 1 m long from the hinge at the origin along +x, 1 m high, 5 cm thick; scale x by len and y by h */
function buildLeaf(mb){mb.box(.5,.5,0,1,1,.05,COL.door);mb.box(.86,.48,0,.1,.03,.11,COL.steel)}
function win(c,w){return{c:c,w:w,sill:.9,top:2.5,glass:true}}
function door(c,w){return{c:c,w:w,sill:0,top:2.2}}
function doorW(r){return r.type==='toilet'?1.2:(r.type==='ward_room'||r.type==='or'||r.type==='icu_bay'||r.type==='treatment_bay'||r.type==='recovery')?1.6:1.2}
/* BOOK-1 map fix: an item near the door wall slides along it until it is clear of the door's opening (free 1.0 m approach) */
function awayU(r,w,u,half){var rot=r.door==='E'||r.door==='W',du=r.dc-(rot?(r.z0+r.z1)/2:(r.x0+r.x1)/2),need=doorW(r)/2+half+.15;if(Math.abs(u-du)>=need)return u;
  var lim=w/2-half-.1,a=du+need,b=du-need,ca=Math.abs(a)<=lim,cb=Math.abs(b)<=lim;if(ca&&cb)return Math.abs(a-u)<=Math.abs(b-u)?a:b;return ca?a:cb?b:-u}
/* corridor dressing stays clear of the doors on its wall */
function corrX(f,x,side){function ok(x){return ROOMS.every(function(q){return q.f!==f||q.door!==side||(side==='S'?q.z1!==-DIM.CO[1]:q.z0!==DIM.CO[1])||Math.abs(q.dc-x)>=doorW(q)/2+1.1})}
  if(ok(x))return x;for(var d=.5;d<4;d+=.5){if(ok(x+d))return x+d;if(ok(x-d))return x-d}return x}
function chapOf(r){return r.topic?TOPICS[r.topic].c:null}
function neighbour(r,s){return ROOMS.some(function(q){if(q===r||q.f!==r.f||q.open)return false;
  return s==='N'?(q.z1===r.z0&&q.x0<=r.x0+.01&&q.x1>=r.x1-.01):(q.x1===r.x0&&q.z0<=r.z0+.01&&q.z1>=r.z1-.01)})}
/* st = static shell. sS / sN = this room's south / north east-west wall as its own fadeable piece. A wall north of a space never hides it
   (the camera looks north), so the north facade stays static; a shared partition is built once, by the room north of it, as that room's south piece. */
function buildRoomShell(st,sS,sN,r){var y=r.f*FH,cx=(r.x0+r.x1)/2,cz=(r.z0+r.z1)/2,ter=r.type==='terrace';
  st.quad([r.x0,y+.01,r.z0],[r.x1,y+.01,r.z0],[r.x1,y+.01,r.z1],[r.x0,y+.01,r.z1],COL[FLOORCOL[r.type]||'f_exam'],[cx,y-1,cz]);
  if(r.open)return;
  'NSWE'.split('').forEach(function(s){var ns=s==='N'||s==='S',axis=ns?'x':'z',a0=ns?r.x0:r.z0+T,a1=ns?r.x1:r.z1-T,len=a1-a0,
      fix=s==='N'?r.z0+T/2:s==='S'?r.z1-T/2:s==='W'?r.x0+T/2:r.x1-T/2,ext=s==='N'?r.z0===-BZ:s==='S'?r.z1===BZ:s==='W'?r.x0===-BX:r.x1===BX,
      out=s==='N'?[0,-1]:s==='S'?[0,1]:s==='W'?[-1,0]:[1,0],m=s==='S'?sS:s==='N'?(ext?st:sN):st,isDoor=r.door===s,gaps=[],bg=[],h=WH;
    if((s==='N'||s==='W')&&!isDoor&&neighbour(r,s))return;
    if(s==='N'&&r.open_n)return;
    if(ter&&(ext||isDoor))h=1.1;
    if(isDoor)gaps.push(door(r.dc,doorW(r)));
    if(ext&&!ter&&r.type!=='toilet'&&!(r.type==='store'&&!r.locker)){
      if(r.entrance&&s==='S'){gaps.push({c:0,w:4.4,sill:0,top:2.8});bg.push({c:0,w:4.4,sill:0,top:1.1})}
      else if(r.ambulance&&s==='E'){gaps.push({c:cz,w:2.4,sill:0,top:2.4});bg.push({c:cz,w:2.4,sill:0,top:1.1})}
      else{var n=Math.max(1,Math.round(len/5.5)),ww=Math.min(3.2,len/n-1.6);for(var i=0;i<n;i++)gaps.push(win(a0+len*(i+.5)/n,ww))}}
    wall(m,axis,a0,a1,fix,T,y,h,COL.wall,gaps);
    if(ext&&r.f===0)wall(m,axis,ns?r.x0:r.z0,ns?r.x1:r.z1,(ns?(s==='N'?-BZ:BZ):(s==='W'?-BX:BX))+(out[0]+out[1])*.04,.1,-.25,1.1,COL.brick,bg);
    if(!isDoor)return;
    /* door frame, open leaves and the plaque in the chapter colour */
    var dm=ns?m:st,dw=doorW(r),px=ns?r.dc:fix,pz=ns?fix:r.dc,ax=ns?1:0,az=ns?0:1,nx=-out[0],nz=-out[1],al=1.25;
    function bx(u,y0,hh,su,sn,col,off){dm.b(px+ax*u+nx*(off||0),y+y0,pz+az*u+nz*(off||0),Math.abs(ax)*su+Math.abs(nx)*sn,hh,Math.abs(az)*su+Math.abs(nz)*sn,col)}
    bx(-dw/2,0,2.2,.1,.2,COL.frame);bx(dw/2,0,2.2,.1,.2,COL.frame);bx(0,2.2,.1,dw+.2,.2,COL.frame);
    var dc=ter?COL.glass:(r.type==='or'||r.type==='icu_bay')?COL.steel:COL.door;
    /* leaves are not baked into the wall: each is its own instance so it can swing (closed along the wall, open 72 degrees into the room) */
    function leaf(u,sg,l){DOORS.push({f:r.f,room:r.id,piece:ns?(s==='S'?'s':'n'):'',x:px+ax*u,y:y,z:pz+az*u,len:l,h:ter?1:2.15,cx:ax*sg,cz:az*sg,nx:nx,nz:nz,col:dc})}
    if(dw>1.3){leaf(-dw/2+.05,1,dw/2-.04);leaf(dw/2-.05,-1,dw/2-.04)}else leaf(-dw/2+.05,1,dw-.1);
    var c=chapOf(r);if((c!==null||r.role)&&!ter)bx(0,2.38,.34,dw+.5,.06,c!==null?COL['c'+c]:COL.role,-(T/2+.03))});
  if(r.entrance){sS.b(-1.6,y,BZ-.05,1.1,2.7,.05,COL.glass);sS.b(1.6,y,BZ-.05,1.1,2.7,.05,COL.glass);sS.b(-2.2,y,BZ-.05,.1,2.8,.2,COL.frame);sS.b(2.2,y,BZ-.05,.1,2.8,.2,COL.frame);st.b(0,y+.03,BZ-.9,3.4,.03,1.4,COL.dark);
    /* canopy with the name sign standing on its front edge; the red cross (double size) is on the wall behind the sign */
    st.b(0,y+2.8,BZ+1.8,11,.25,3.5,COL.white);st.b(-5.1,y,BZ+3,.3,2.8,.3,COL.steel);st.b(5.1,y,BZ+3,.3,2.8,.3,COL.steel);
    st.b(0,y+3.05,BZ+3.3,10.6,1.25,.16,COL.blue);
    /* sign supports: a post behind the board, a shorter rear post, a tie bar between them and a foot plate, four times along the sign */
    [-4.8,-1.9,1.9,4.8].forEach(function(q){st.b(q,y+3.05,BZ+3.14,.14,1.25,.14,COL.steel);st.b(q,y+3.05,BZ+2.4,.14,.65,.14,COL.steel);st.b(q,y+3.62,BZ+2.77,.1,.1,.88,COL.steel);st.b(q,y+3.05,BZ+2.77,.3,.06,1.1,COL.frame)});
    st.b(0,y+3.95,BZ+3.17,10.4,.1,.1,COL.steel);
    /* red cross standing on the centre of the canopy roof, on a plinth */
    st.b(0,y+3.05,BZ+1.8,1.9,.3,1.1,COL.white);st.box(0,y+5.05,BZ+1.8,3.4,1.2,.6,COL.red);st.box(0,y+5.05,BZ+1.8,1.2,3.4,.6,COL.red)}}

function corridorItem(mb,k,x,y,z,s){ /* s = +1 item stands against the north wall, -1 south */
  if(k===0){mb.b(x,y+.4,z,1.6,.08,.45,COL.wood);mb.b(x,y+.48,z-s*.2,1.6,.4,.06,COL.wood);mb.b(x-.7,y,z,.08,.4,.4,COL.frame);mb.b(x+.7,y,z,.08,.4,.4,COL.frame)}
  else if(k===1){mb.cyl([x,y,z],.26,.3,.45,8,COL.pot);mb.cyl([x,y+.45,z],.5,.08,1.1,7,COL.plant);mb.cyl([x,y+.9,z],.34,.04,.8,6,COL.leaf2)}
  else if(k===2){mb.cyl([x-.3,y,z],.17,.17,.55,8,COL.steel,COL.dark);mb.box(x+.3,y+1.2,z-s*.2,.18,.3,.1,COL.white);mb.box(x+.3,y+1.02,z-s*.17,.06,.08,.06,COL.blue);mb.box(x+.9,y+1.1,z-s*.2,.22,.5,.14,COL.red)}
  else if(k===3){mb.b(x,y+.25,z,.55,.06,.5,COL.dark);mb.b(x,y+.31,z-s*.22,.55,.5,.05,COL.dark);mb.cyl([x-.3,y,z],.02,.02,.3,4,COL.steel);[-1,1].forEach(function(q){mb.box(x+q*.3,y+.28,z+.02,.04,.56,.56,COL.steel)})}
  else if(k===4){mb.box(x,y+1.55,z-s*.24,1,.7,.03,COL.white);mb.box(x,y+1.55,z-s*.225,.9,.6,.02,COL[['c0','c2','c1','c4','c3'][Math.abs(Math.round(x))%5]]);mb.box(x,y+1.7,z-s*.21,.5,.06,.02,COL.white);mb.box(x,y+1.5,z-s*.21,.7,.04,.02,COL.white)}
  else{mb.b(x,y+.5,z,1.9,.1,.6,COL.steel);mb.b(x,y+.6,z,1.85,.1,.56,COL.sheet);mb.b(x+.7,y+.7,z,.35,.08,.4,COL.white);[-.8,.8].forEach(function(q){mb.b(x+q,y,z,.06,.5,.06,COL.frame)});mb.b(x-.9,y+.6,z,.04,.9,.04,COL.steel)}}
function buildCore(mb,f){var y=f*FH,i,W=2*BX,D=2*BZ;
  /* floor slab: solid on the ground, with the atrium void above */
  if(f===0)mb.box(0,-.15,0,W,.3,D,COL.slab);else{mb.box(0,y-.15,-(BZ+AZ)/2,W,.3,BZ-AZ,COL.slab);mb.box(0,y-.15,(BZ+AZ)/2,W,.3,BZ-AZ,COL.slab);[-1,1].forEach(function(q){mb.box(q*(BX+16)/2,y-.15,0,BX-16,.3,2*AZ,COL.slab);mb.box(q*12.5,y-.15,-2.5,7,.3,11,COL.slab)})}
  CORR.forEach(function(c){mb.quad([c[0],y+.01,c[1]],[c[2],y+.01,c[1]],[c[2],y+.01,c[3]],[c[0],y+.01,c[3]],COL.corr,[0,y-1,0])});
  if(f===0){mb.b(0,-.24,BZ+1.1,W+4,.12,2.2,COL.curb);mb.b(0,-.24,-BZ-1,W+4,.12,2,COL.curb);mb.b(-BX-1,-.24,0,2,.12,D,COL.curb);mb.b(BX+1,-.24,0,2,.12,D,COL.curb)}
  else{mb.box(0,y-.15,BZ+.05,W+.3,.34,.12,COL.trim);mb.box(0,y-.15,-BZ-.05,W+.3,.34,.12,COL.trim);mb.box(-BX-.05,y-.15,0,.12,.34,D,COL.trim);mb.box(BX+.05,y-.15,0,.12,.34,D,COL.trim);
    /* glass balustrade on the two open edges of the atrium void */
    [-1,1].forEach(function(q){mb.b(0,y,q*(AZ+.12),2*AX,1.05,.05,COL.glass);mb.b(0,y+1.05,q*(AZ+.12),2*AX,.06,.1,COL.steel);for(i=-3;i<=3;i++)mb.b(i*3,y,q*(AZ+.12),.07,1.05,.07,COL.steel)})}
  [[-BX,-BZ],[BX,-BZ],[-BX,BZ],[BX,BZ]].forEach(function(q){mb.b(q[0]+(q[0]<0?-.06:.06),y,q[1]+(q[1]<0?-.06:.06),.5,WH,.5,COL.wallx)});
  ALCOVES.forEach(function(a,k){if(a.f!==f)return;mb.quad([a.x0,y+.01,a.z0],[a.x1,y+.01,a.z0],[a.x1,y+.01,a.z1],[a.x0,y+.01,a.z1],COL.f_recep,[0,y-1,0]);
    var ns=a.side==='N'||a.side==='S',lo=ns?a.x0:a.z0,hi=ns?a.x1:a.z1,mid=(lo+hi)/2,wc=a.side==='N'?a.z0:a.side==='S'?a.z1:a.side==='W'?a.x0:a.x1,n=(a.side==='N'||a.side==='W')?1:-1,col=COL[['c0','c2','c1','c3'][k%4]];
    function at(t,off,sy,su,h,sn,c){if(ns)mb.b(t,y+sy,wc+n*off,su,h,sn,c);else mb.b(wc+n*off,y+sy,t,sn,h,su,c)}
    for(var t=lo+.55;t<mid-1.2;t+=.75){[t,lo+hi-t].forEach(function(q){at(q,.42,0,.6,.45,.5,col);at(q,.14,.45,.6,.45,.08,col)})}
    at(lo+.45,1.5,0,.4,.4,.4,COL.pot);at(lo+.45,1.5,.4,.7,.9,.7,COL.plant);at(hi-.5,1.45,0,.4,1,.4,COL.white);at(hi-.5,1.45,1,.26,.34,.26,COL.water)});
  /* corridor dressing against the east-west walls, between doors */
  var kN=f,kS=f+3;ROOMS.forEach(function(r){if(r.f!==f||Math.abs(r.x0)>=DIM.CO[0]-1)return;if(r.door==='S'&&r.z1===-DIM.CO[1])corridorItem(mb,(kN++)%6,corrX(f,r.x0,'S'),y,-DIM.CO[1]+.3,1);
    if(r.door==='N'&&r.z0===DIM.CO[1]&&!(f===0&&Math.abs(r.x0)<6))corridorItem(mb,(kS++)%6,corrX(f,r.x0,'N'),y,DIM.CO[1]-.3,-1)});
  mb.box(-12.5,y+3,AZ+1.5,2.6,.5,.08,COL.blue);mb.box(-12.5,y+3.05,AZ+1.55,2.2,.1,.02,COL.white);mb.box(-12.5,y+2.9,AZ+1.55,1.4,.08,.02,COL.white);mb.b(-12.5,y+3.25,AZ+1.5,.04,.45,.04,COL.steel)}

/* ---- furniture: room-local u (across) and v (towards the wall opposite the door), mapped to world for any door side ---- */
function loc(r){var d=r.door,bx=d==='E'?-1:d==='W'?1:0,bz=d==='S'?-1:d==='N'?1:0,rot=bx!==0,cx=(r.x0+r.x1)/2,cz=(r.z0+r.z1)/2,y=r.f*FH+.01,
    w=rot?r.z1-r.z0:r.x1-r.x0,dep=rot?r.x1-r.x0:r.z1-r.z0,k=(dep/2-.1)/4.25,ux=rot?0:1,uz=rot?1:0;
  function P(u,dy,v){return[cx+u*ux+v*k*bx,y+dy,cz+u*uz+v*k*bz]}
  return{y:y,w:w,d:dep,P:P,b:function(mb,u,v,y0,su,h,sv,col){var p=P(u,y0,v);mb.b(p[0],p[1],p[2],rot?sv:su,h,rot?su:sv,col)}}}
function bed(mb,L,u,v,col){L.b(mb,u,v,.35,1,.12,2.1,COL.bedframe);L.b(mb,u,v,.47,.95,.18,2.05,COL.sheet);L.b(mb,u,v+.75,.65,.6,.12,.4,COL.white);L.b(mb,u,v-.35,.66,.97,.05,1.25,col||COL.blanket);
  L.b(mb,u,v+1.08,0,1.05,1,.08,COL.bedframe);[-.42,.42].forEach(function(a){L.b(mb,u+a,v-.9,0,.08,.35,.08,COL.frame);L.b(mb,u+a,v+.9,0,.08,.35,.08,COL.frame)})}
function chair(mb,L,u,v,col){L.b(mb,u,v,0,.5,.45,.5,col||COL.blue);L.b(mb,u,v+.22,.45,.5,.5,.08,col||COL.blue)}
function desk(mb,L,u,v,w){w=w||1.8;L.b(mb,u,v,.72,w,.06,.8,COL.wood);L.b(mb,u-w/2+.1,v,0,.08,.72,.7,COL.wood2);L.b(mb,u+w/2-.1,v,0,.08,.72,.7,COL.wood2);
  L.b(mb,u,v-.1,.78,.5,.36,.05,COL.screen);L.b(mb,u,v-.07,.82,.42,.28,.02,COL.screenOn);chair(mb,L,u,v+.75,COL.dark)}
function monitor(mb,L,u,v){L.b(mb,u,v,0,.06,1.5,.06,COL.steel);L.b(mb,u,v,1.2,.5,.4,.12,COL.screen);L.b(mb,u,v-.07,1.25,.42,.3,.02,COL.green);L.b(mb,u,v,0,.4,.05,.4,COL.steel)}
function shelf(mb,L,u,v,w,cols){L.b(mb,u,v,0,w,2,.4,COL.wood2);for(var k=0;k<4;k++)for(var i=0;i<Math.floor(w/.5);i++)L.b(mb,u-w/2+.25+i*.5,v-.22,.25+k*.45,.36,.3,.06,COL[cols[(i+k)%cols.length]])}
function cabinet(mb,L,u,v,w){L.b(mb,u,v,0,w,.9,.6,COL.white);L.b(mb,u,v,.9,w+.04,.05,.64,COL.steel)}
function plant(mb,L,u,v){L.b(mb,u,v,0,.4,.4,.4,COL.pot);mb.cyl(L.P(u,.4,v),.45,.05,.9,6,COL.plant)}
function trolleyTower(mb,L,u,v){L.b(mb,u,v,0,.6,1.5,.5,COL.steel);L.b(mb,u,v-.26,1.0,.5,.4,.03,COL.screenOn);L.b(mb,u,v,1.5,.7,.5,.08,COL.screen);L.b(mb,u,v-.05,1.55,.6,.4,.02,COL.pink)}
/* ---- extra props: each is placed at a room anchor (front corners, mid walls, centre) ---- */
var PROP={
 sofa:function(mb,L,u,v){L.b(mb,u,v,0,1.9,.42,.8,COL.c0);L.b(mb,u,v-.32,.42,1.9,.45,.18,COL.c0);L.b(mb,u-.88,v,.42,.16,.2,.8,COL.c0);L.b(mb,u+.88,v,.42,.16,.2,.8,COL.c0)},
 table:function(mb,L,u,v){L.b(mb,u,v,.36,1,.06,.6,COL.wood);L.b(mb,u,v,0,.7,.36,.3,COL.wood2);L.b(mb,u-.2,v,.42,.3,.03,.22,COL.c4);L.b(mb,u+.25,v+.1,.42,.22,.03,.3,COL.white)},
 cooler:function(mb,L,u,v){L.b(mb,u,v,0,.36,1,.36,COL.white);mb.cyl(L.P(u,1,v),.15,.13,.4,8,COL.water)},
 vending:function(mb,L,u,v){L.b(mb,u,v,0,.95,1.9,.7,COL.dark);L.b(mb,u-.12,v+.36,.5,.6,1.2,.03,COL.glass);L.b(mb,u+.33,v+.36,.9,.18,.5,.03,COL.c4);L.b(mb,u,v+.36,.15,.6,.2,.03,COL.screen)},
 fridge:function(mb,L,u,v){L.b(mb,u,v,0,.7,1.8,.65,COL.white);L.b(mb,u+.28,v+.33,.9,.05,.5,.04,COL.steel);L.b(mb,u,v+.33,1.15,.7,.03,.02,COL.steel)},
 crash:function(mb,L,u,v){L.b(mb,u,v,.12,.7,.85,.5,COL.red);for(var i=0;i<4;i++)L.b(mb,u,v+.26,.2+i*.19,.6,.03,.02,COL.white);L.b(mb,u,v,.97,.72,.04,.52,COL.steel);L.b(mb,u-.15,v,1.01,.3,.2,.25,COL.screen);L.b(mb,u,v,0,.6,.12,.4,COL.dark)},
 oxygen:function(mb,L,u,v){[-.14,.14].forEach(function(q){mb.cyl(L.P(u+q,0,v),.11,.11,1.15,8,COL.green);mb.cyl(L.P(u+q,1.15,v),.05,.05,.16,6,COL.steel)});L.b(mb,u,v,0,.5,.3,.3,COL.steel)},
 ultrasound:function(mb,L,u,v){L.b(mb,u,v,0,.55,.8,.6,COL.white);L.b(mb,u,v,.8,.6,.06,.5,COL.steel);L.b(mb,u,v-.12,.86,.08,.5,.08,COL.steel);L.b(mb,u,v-.12,1.3,.6,.42,.06,COL.screen);L.b(mb,u,v-.08,1.34,.52,.34,.02,COL.screenOn);L.b(mb,u+.36,v+.1,.5,.06,.3,.06,COL.dark)},
 recliner:function(mb,L,u,v){L.b(mb,u,v,.3,.75,.16,.8,COL.c2);L.b(mb,u,v+.6,.3,.75,.7,.2,COL.c2);L.b(mb,u,v-.65,.25,.7,.12,.5,COL.c2);L.b(mb,u-.42,v,.46,.1,.2,.8,COL.steel);L.b(mb,u+.42,v,.46,.1,.2,.8,COL.steel);L.b(mb,u,v,0,.5,.3,.5,COL.frame);
   L.b(mb,u+.75,v+.5,0,.04,1.8,.04,COL.steel);L.b(mb,u+.75,v+.5,1.5,.18,.3,.06,COL.water);L.b(mb,u+.75,v+.5,0,.35,.04,.35,COL.steel)},
 board:function(mb,L,u,v){L.b(mb,u,v,.8,1.7,1.05,.05,COL.white);L.b(mb,u,v,.76,1.8,.05,.09,COL.steel);L.b(mb,u-.8,v,0,.05,.8,.4,COL.steel);L.b(mb,u+.8,v,0,.05,.8,.4,COL.steel);L.b(mb,u-.3,v+.03,1.3,.7,.04,.01,COL.c0);L.b(mb,u+.2,v+.03,1.1,.9,.04,.01,COL.c4)},
 model:function(mb,L,u,v){L.b(mb,u,v,0,.5,.9,.5,COL.wood2);L.b(mb,u,v,.9,.42,.6,.26,COL.pink);L.b(mb,u-.08,v+.14,1.05,.14,.2,.03,COL.c4);L.b(mb,u+.07,v+.14,1.2,.16,.14,.03,COL.c1);L.b(mb,u,v+.14,.95,.2,.1,.03,COL.c3);mb.cyl(L.P(u,1.5,v),.12,.1,.2,7,COL.skin)},
 centrifuge:function(mb,L,u,v){L.b(mb,u,v,0,.8,.8,.6,COL.white);mb.cyl(L.P(u-.12,.8,v),.24,.22,.26,10,COL.steel,COL.dark);L.b(mb,u+.26,v,.8,.16,.3,.16,COL.c2)},
 coats:function(mb,L,u,v){L.b(mb,u,v,0,.05,1.75,.05,COL.steel);L.b(mb,u,v,1.7,1.3,.05,.05,COL.steel);L.b(mb,u,v,0,.5,.04,.5,COL.steel);[-.45,-.15,.2,.5].forEach(function(q,i){L.b(mb,u+q,v,.9,.24,.78,.1,COL[['coat','scrub','coat','c0'][i]])})},
 aquarium:function(mb,L,u,v){L.b(mb,u,v,0,1.4,.75,.5,COL.wood2);L.b(mb,u,v,.75,1.36,.6,.46,COL.glass);L.b(mb,u,v,.77,1.3,.06,.4,COL.yellow);L.b(mb,u-.3,v,1,.12,.06,.03,COL.orange);L.b(mb,u+.25,v,1.15,.1,.05,.03,COL.c4);L.b(mb,u+.5,v,.83,.05,.3,.05,COL.plant);L.b(mb,u,v,1.35,1.4,.05,.5,COL.dark)},
 palm:function(mb,L,u,v){mb.cyl(L.P(u,0,v),.3,.36,.5,8,COL.pot);mb.cyl(L.P(u,.5,v),.07,.05,1.3,5,COL.trunk);mb.cyl(L.P(u,1.5,v),.8,.05,.9,7,COL.leaf,COL.leaf2);mb.cyl(L.P(u,1.9,v),.5,.03,.8,6,COL.leaf2)},
 screen:function(mb,L,u,v){for(var i=-1;i<=1;i++){L.b(mb,u+i*.62,v+(i?0.12:0),.15,.6,1.6,.03,COL.blanket2);L.b(mb,u+i*.62-.3,v+(i?0.12:0),0,.03,1.8,.03,COL.steel)}L.b(mb,u+.92,v+.12,0,.03,1.8,.03,COL.steel)},
 scale:function(mb,L,u,v){L.b(mb,u,v,0,.4,.08,.5,COL.dark);L.b(mb,u,v+.2,.08,.06,1.3,.06,COL.steel);L.b(mb,u,v+.2,1.38,.3,.12,.1,COL.white);L.b(mb,u,v+.2,1.5,.04,.6,.04,COL.steel)},
 cart:function(mb,L,u,v){L.b(mb,u,v,.15,.6,.75,.45,COL.steel);[0,1,2].forEach(function(i){L.b(mb,u,v+.23,.22+i*.22,.5,.16,.02,COL[['c0','c2','c1'][i]])});L.b(mb,u,v,.9,.62,.03,.47,COL.white);L.b(mb,u-.15,v,.93,.12,.18,.12,COL.c4);L.b(mb,u+.15,v,.93,.1,.12,.1,COL.white);L.b(mb,u,v,0,.5,.15,.35,COL.dark)},
 scopes:function(mb,L,u,v){L.b(mb,u,v,0,1,2,.4,COL.white);L.b(mb,u,v+.2,.3,.9,1.6,.03,COL.glass);for(var i=0;i<4;i++){L.b(mb,u-.33+i*.22,v+.23,.5,.04,1.3,.02,COL.dark);L.b(mb,u-.33+i*.22,v+.23,1.75,.1,.08,.02,COL.dark)}},
 printer:function(mb,L,u,v){L.b(mb,u,v,0,.7,.6,.5,COL.wood);L.b(mb,u,v,.6,.5,.25,.42,COL.white);L.b(mb,u,v+.1,.85,.34,.02,.26,COL.white);L.b(mb,u+.15,v+.22,.68,.12,.06,.02,COL.screenOn)},
 podium:function(mb,L,u,v){L.b(mb,u,v,0,.7,1.05,.5,COL.wood2);L.b(mb,u,v,1.05,.8,.06,.6,COL.wood);L.b(mb,u,v+.05,1.11,.3,.02,.22,COL.white);L.b(mb,u+.25,v-.1,1.11,.03,.25,.03,COL.dark)},
 trophy:function(mb,L,u,v){L.b(mb,u,v,0,1.3,1.5,.35,COL.wood2);[0,1].forEach(function(k){L.b(mb,u,v+.02,.55+k*.5,1.2,.03,.34,COL.wood);[-.4,0,.4].forEach(function(q,i){mb.cyl(L.P(u+q,.58+k*.5,v),.05,.1,.22,6,COL.yellow);L.b(mb,u+q,v,.58+k*.5,.14,.04,.14,COL[(i+k)%2?'dark':'c0'])})})},
 lightbox:function(mb,L,u,v){L.b(mb,u,v,0,.06,1.2,.06,COL.steel);L.b(mb,u,v,0,.5,.05,.4,COL.steel);L.b(mb,u,v,1.2,.9,.65,.08,COL.white);L.b(mb,u-.2,v+.05,1.28,.36,.5,.01,COL.screen);L.b(mb,u+.22,v+.05,1.28,.36,.5,.01,COL.dark)},
 dental:function(mb,L,u,v){L.b(mb,u,v,0,.6,.45,.7,COL.steel);L.b(mb,u,v-.2,.45,.6,.14,1.1,COL.c2);L.b(mb,u,v+.65,.5,.58,.5,.16,COL.c2);L.b(mb,u,v+.95,.95,.3,.1,.26,COL.c2);L.b(mb,u,v-.95,.35,.5,.1,.4,COL.c2);
   L.b(mb,u+.6,v+.3,0,.07,1.9,.07,COL.steel);L.b(mb,u+.3,v+.3,1.85,.7,.06,.06,COL.steel);mb.cyl(L.P(u,1.65,v+.3),.2,.14,.14,8,COL.white,COL.yellow);L.b(mb,u-.6,v+.2,0,.3,.85,.3,COL.white);mb.cyl(L.P(u-.6,.85,v+.2),.14,.12,.08,8,COL.steel,COL.water);L.b(mb,u+.62,v-.3,.9,.4,.04,.3,COL.steel)}};
var EXTRA={g_stoma:['dental c','cart ml','lightbox fr'],g_salivare:['ultrasound ml','lightbox fr','scale fl'],g_asteptare:['sofa c','table c2','aquarium mr','vending fr','cooler fl','palm ml'],
 g_reflux1:['scale fl','model mr'],g_reflux2:['lightbox fr'],g_dispepsie:['ultrasound ml','scale fr'],g_gastrite:['model fr'],g_fiziologie:['recliner c','cart ml','printer fr','oxygen fl','screen c3'],
 g_triaj:['crash ml','oxygen fl','scale fr','screen c','cart mr'],g_amfiteatru:['board sb'],g_farmacie:['fridge fr','cart ml'],g_tranzit:['screen c','scale fr'],g_garda:['crash mr','oxygen fr','printer c2','palm ml'],
 e_microbio:['fridge fr','centrifuge ml'],e_anapat:['centrifuge fr','fridge ml'],e_respirator:['recliner ml','printer fr'],e_histo:['fridge fr','centrifuge ml','board fl'],e_analize:['centrifuge ml','printer fr'],e_vestiar:['coats fr','coats fl'],
 e_primire:['sofa c','cooler fr','screen c3','palm ml','table c2'],e_endo1:['scopes fr','oxygen fl'],e_endo2:['scopes fr','cart fl'],e_decontam:['scopes mr','scopes ml'],e_endoter:['scopes fr','crash fl','oxygen ml'],e_endo3:['scopes fr','cart fl'],
 e_recuperare:['oxygen fr','cart c','screen c3'],e_farmclin:['fridge fr','cart ml','printer fl'],e_functional:['recliner c','ultrasound mr'],e_colono:['scopes fr','oxygen fl'],e_radiologie:['lightbox fr','lightbox fl','printer mr'],
 w_epi:['board fl','printer fr'],w_genetica:['fridge fr','centrifuge ml','board fl'],w_imuno:['centrifuge fr','fridge ml'],w_interdis:['model fr','sofa c'],w_monitor:['scale fl','ultrasound ml','palm fr'],
 w_g1:['screen c','cart fr'],w_g2:['cart fr'],w_g3:['oxygen fr'],w_b1:['cart fr'],w_b2:['oxygen fr'],w_b3:['screen c','cart fr'],w_b4:['oxygen fr'],w_statie:['cart fr','crash fl'],w_complicatii:['crash fr','oxygen fl','ultrasound c3'],
 w_perfuzii:['recliner c','recliner c2','recliner c3','cart fr','cooler fl'],w_intermediar:['oxygen fr','crash fl','ultrasound c','cart mr'],
 o_genetica:['board fl','sofa c','model fr'],o_onco:['model fr','lightbox fl'],o_sef:['sofa c','trophy fr','palm fl'],o_comisie:['podium fr','board fl','cooler mr'],o_liga:['trophy fr','trophy fl','palm mr'],o_birou:['sofa c','palm fr','trophy fl'],
 o_so1:['crash fr','oxygen fl','lightbox mr'],o_so2:['oxygen fr','cart fl'],o_so3:['cart fr','lightbox fl'],o_so4:['oxygen fr','crash fl'],o_filtru:['coats fr','coats mr'],o_recuperare:['oxygen fr','cart c','screen c3','crash fl'],
 o_biblioteca:['sofa c3','palm fr'],o_examen:['podium fr','board fl','cooler mr'],o_terasa:['sofa c','table c2','palm fr','palm fl']};
function extras(mb,r,L){var list=EXTRA[r.id]||(r.role==='garda_case'?[(parseInt(r.id.slice(-1))%2?'oxygen':'cart')+' fr']:[]),w=L.w,rot=r.door==='E'||r.door==='W',du=r.dc-(rot?(r.z0+r.z1)/2:(r.x0+r.x1)/2);
  var A={fr:[w/2-.9,-3.2],fl:[-w/2+1.9,-3.2],mr:[w/2-.9,-1],ml:[-w/2+.9,-1],c:[0,-1.5],c2:[Math.min(2,w/2-1.2),-1.5],c3:[-Math.min(2,w/2-1.2),-1.5],sl:[-w/2+1.4,3.3],sr:[w/2-1.4,3.3],sb:[-w/2+1.4,4.0]};
  list.forEach(function(it){var p=it.split(' '),a=A[p[1]],u=a[0],v=a[1];if(v<-2.5&&Math.abs(u-du)<1.5){u=-u;if(Math.abs(u-du)<1.5)return}if(w<4.5&&p[1].charAt(0)==='c')return;PROP[p[0]](mb,L,u,v)})}
function furnish(mb,r){furnishBase(mb,r);var L=loc(r),w=L.w,t=r.type,n=Math.abs(Math.round(r.x0*3+r.f*7));if(t==='toilet'||(t==='store'&&!r.locker))return;if(!r.open&&!r.entrance)extras(mb,r,L);if(r.locker)return;
  if(t!=='terrace'){mb.cyl(L.P(awayU(r,w,-w/2+.5,.2),0,-3.6),.16,.16,.5,7,COL.steel,COL.dark);                                     /* bin */
    L.b(mb,w/2-.13,-1.2,1.3,.03,.8,1.1,COL.white);L.b(mb,w/2-.15,-1.2,1.36,.03,.68,.98,COL[['c0','c1','c2','c3','c4'][n%5]]);L.b(mb,w/2-.17,-1.2,1.75,.02,.08,.6,COL.white);   /* poster */
    L.b(mb,-w/2+.13,-1.5,1.9,.03,.4,.4,COL.white);L.b(mb,-w/2+.15,-1.5,2.08,.02,.03,.03,COL.dark)}                                 /* clock */
  if(t==='exam'||t==='lab'||t==='ward_room'||t==='icu_bay'){L.b(mb,-w/2+.4,-2.6,0,.55,.85,.5,COL.white);L.b(mb,-w/2+.4,-2.6,.85,.5,.05,.42,COL.steel);L.b(mb,-w/2+.2,-2.6,.9,.05,.25,.05,COL.steel)}   /* sink */
  if(t==='ward_room'){[-1,1].forEach(function(q){var u=q*(w/2-1.3);
      L.b(mb,u+q*-.75,3.3,0,.04,1.8,.04,COL.steel);L.b(mb,u+q*-.75,3.3,1.5,.18,.3,.06,COL.water);L.b(mb,u+q*-.75,3.3,0,.35,.04,.35,COL.steel);L.b(mb,u+q*-.62,1.2,0,.03,2,2.6,COL.blanket2)})}
  if(t==='office'||t==='meeting')L.b(mb,0,-.3,0,Math.min(4,w-2.5),.02,2.6,COL[n%2?'c3':'c0']);
}
function furnishBase(mb,r){var L=loc(r),w=L.w,ch=chapOf(r),k=r.slot,t=r.type,i;
  if(t==='toilet'){for(i=0;i<3;i++){L.b(mb,-w/2+1+i*1.6,3.2,0,1.3,2,.05,COL.white);L.b(mb,-w/2+1+i*1.6,3.7,0,.4,.45,.5,COL.white)}L.b(mb,w/2-.5,-1,0,.5,.85,2,COL.white);return}
  if(r.stairs){for(i=0;i<8;i++)L.b(mb,0,-3+i*.8,0,2.4,.2+i*.2,.8,COL.slab);return}
  if(r.locker){for(i=0;i<Math.floor(w-1);i++)L.b(mb,-w/2+1+i,3.8,0,.9,1.9,.5,COL[i%2?'c0':'steel']);L.b(mb,0,.5,.4,w-4,.08,.5,COL.wood);L.b(mb,0,.5,0,.3,.4,.4,COL.frame);return}
  if(t==='store'){shelf(mb,L,0,3.9,w-1,['c1','white','c2','steel']);shelf(mb,L,0,-1,w-2,['white','c0','steel']);return}
  if(r.entrance){plant(mb,L,-4.2,2.8);plant(mb,L,4.2,2.8);[-3.4,3.4].forEach(function(q){L.b(mb,q,-1,.4,.5,.08,1.8,COL.wood);L.b(mb,q,-1,0,.4,.4,.2,COL.frame)});return}
  if(t==='ward_room'){bed(mb,L,-w/2+1.3,2.6,ch===3||ch===4?COL.blanket2:COL.blanket);bed(mb,L,w/2-1.3,2.6,COL.blanket);if(w>9.5){bed(mb,L,-w/2+4,2.6,COL.blanket2);bed(mb,L,w/2-4,2.6,COL.pink);monitor(mb,L,-w/2+5,3.8);monitor(mb,L,w/2-5,3.8)}monitor(mb,L,-w/2+2.3,3.8);monitor(mb,L,w/2-2.3,3.8);cabinet(mb,L,0,3.8,1);chair(mb,L,0,-2,COL.orange);return}
  if(t==='icu_bay'){for(i=-1;i<=1;i+=2){bed(mb,L,i*2,2.6,COL.blanket2);monitor(mb,L,i*2+.9,3.8);monitor(mb,L,i*2-.9,3.8)}L.b(mb,0,-2.5,0,2.4,1,.7,COL.white);return}
  if(t==='treatment_bay'&&r.role==='garda_case'){bed(mb,L,0,2.6,COL.pink);monitor(mb,L,.95,3.8);L.b(mb,-w/2+.25,0,0,.05,2.2,6,COL.blanket2);return}
  if(t==='treatment_bay'){for(i=0;i<3;i++){L.b(mb,-w/2+1.5+i*2.5,3,0,.9,.5,1.5,COL.scrub);L.b(mb,-w/2+1.5+i*2.5,3.7,.5,.9,.8,.15,COL.scrub);L.b(mb,-w/2+2.3+i*2.5,3.5,0,.05,1.9,.05,COL.steel);L.b(mb,-w/2+2.3+i*2.5,3.5,1.6,.25,.35,.08,COL.yellow)}desk(mb,L,0,-2);return}
  if(t==='recovery'){for(i=0;i<3;i++){bed(mb,L,-w/2+1.3+i*((w-2.6)/2),2.6,COL.blanket2)}cabinet(mb,L,awayU(r,w,0,1),-3.6,2);return}
  if(t==='or'){L.b(mb,0,.5,0,.5,.8,.5,COL.steel);L.b(mb,0,.5,.8,.8,.12,2.1,COL.dark);L.b(mb,0,.5,.92,.75,.05,2,COL.scrub);
    mb.cyl(L.P(.3,2.6,.5),.7,.5,.18,10,COL.white,COL.yellow);L.b(mb,.3,.5,2.78,.08,.9,.08,COL.steel);
    L.b(mb,-2.2,1.6,0,.7,1.5,.6,COL.steel);L.b(mb,-2.2,1.3,1.1,.5,.35,.03,COL.green);L.b(mb,2.3,-.5,0,1.2,.9,.6,COL.steel);L.b(mb,2.3,-.5,.9,1,.04,.5,COL.scrub);cabinet(mb,L,0,3.9,w-2);return}
  if(t==='scrub'||t==='sterile'){for(i=0;i<3;i++){L.b(mb,-w/2+.5,-2.5+i*2.4,0,.6,.9,1.6,COL.steel);L.b(mb,-w/2+.5,-2.5+i*2.4,.9,.5,.06,1.2,COL.water)}cabinet(mb,L,w/2-.5,0,.6);L.b(mb,w/2-.5,2.5,0,.6,1.8,1.6,COL.white);return}
  if(k==='scope'){L.b(mb,0,.6,0,.7,.7,2,COL.steel);L.b(mb,0,.6,.7,.75,.12,2.05,COL.sheet);L.b(mb,0,1.4,.82,.5,.1,.35,COL.white);trolleyTower(mb,L,1.5,1.6);trolleyTower(mb,L,-1.6,2.2);
    cabinet(mb,L,0,3.9,w-1.5);chair(mb,L,-1.2,-.4,COL.dark);return}
  if(k==='bench'){L.b(mb,0,3.8,0,w-1,.9,.7,COL.white);L.b(mb,0,3.8,.9,w-.9,.05,.75,COL.dark);for(i=0;i<Math.floor(w-1);i++){L.b(mb,-w/2+1+i,3.8,.95,.16,.3+.1*(i%3),.16,COL[['c1','c2','c3','c4','c0'][i%5]])}
    L.b(mb,0,.3,0,2.6,.9,1.1,COL.white);L.b(mb,0,.3,.9,2.7,.05,1.2,COL.dark);L.b(mb,-.6,.3,.95,.3,.5,.3,COL.frame);L.b(mb,.6,.3,.95,.45,.4,.4,COL.steel);chair(mb,L,0,-.8,COL.dark);
    L.b(mb,w/2-.5,-1,0,.7,1.9,.9,COL.steel);return}
  if(k==='counter'){L.b(mb,0,-.5,0,w-2,1.05,.7,COL.wood);L.b(mb,0,-.5,1.05,w-1.8,.06,.8,COL.white);shelf(mb,L,0,3.9,w-1,['c0','c1','c2','c4','white']);shelf(mb,L,-w/2+.35,1.5,.5,['c3','c2']);
    L.b(mb,.8,-.5,1.11,.4,.3,.3,COL.screen);L.b(mb,0,3.55,2.55,.9,.3,.06,COL.green);L.b(mb,0,3.55,2.25,.3,.9,.06,COL.green);return}
  if(k==='screen'&&t==='imaging'){mb.cyl(L.P(-1,0,2),1.3,1.3,1.1,14,COL.white,COL.dark);mb.cyl(L.P(-1,1.1,2),.75,.75,.02,14,COL.screen);L.b(mb,-1,-.2,.5,.7,.15,2.6,COL.sheet);L.b(mb,-1,-.2,0,.5,.5,1,COL.steel);
    desk(mb,L,2,3,2);L.b(mb,2,4.1,1.2,1.6,1,.06,COL.screen);L.b(mb,2,4.05,1.3,1.4,.8,.02,COL.screenOn);return}
  if(k==='screen'){desk(mb,L,1.2,2.8,2);L.b(mb,1.2,4.1,1.2,1.6,1,.06,COL.screen);L.b(mb,1.2,4.05,1.3,1.4,.8,.02,COL.screenOn);L.b(mb,-1.5,1,0,.7,.7,2,COL.steel);L.b(mb,-1.5,1,.7,.75,.12,2.05,COL.sheet);trolleyTower(mb,L,-1.5,3);return}
  if(k==='or_table')return;
  if(r.id==='g_amfiteatru'){L.b(mb,0,4.1,1,4.5,1.6,.06,COL.screen);L.b(mb,0,4.05,1.1,4.2,1.4,.02,COL.white);L.b(mb,2.6,3,0,.7,1.1,.5,COL.wood);for(i=0;i<4;i++){L.b(mb,0,1.6-i*1.15,0,w-3.2,.25+i*.18,1.1,COL.wood2);for(var j=0;j<6;j++){var yy=.25+i*.18,cc=COL['c'+(j%2?0:1)];L.b(mb,-2.5+j,1.5-i*1.15,yy,.45,.45,.45,cc);L.b(mb,-2.5+j,1.5-i*1.15-.2,yy+.45,.45,.45,.08,cc)}}return}
  if(r.id==='g_lobby'){L.b(mb,-3.5,-1.5,0,4,1.05,.8,COL.wood);L.b(mb,-3.5,-1.5,1.05,4.2,.06,.9,COL.white);chair(mb,L,-3.5,-2.3,COL.dark);
    for(i=0;i<5;i++){chair(mb,L,2+i*.9,-3.6,COL.c0);chair(mb,L,2+i*.9,-1.6,COL.c2)}L.b(mb,-5,3.4,0,3,2.2,.12,COL.screen);L.b(mb,-5,3.36,.3,2.7,1.7,.02,COL.yellow);L.b(mb,5,3.4,0,3,2.2,.12,COL.screen);L.b(mb,5,3.36,.3,2.7,1.7,.02,COL.green);
    [[-7.6,1],[7.6,1],[-7.6,-2.4],[7.6,-2.4]].forEach(function(q){L.b(mb,q[0],q[1],0,1.6,.5,1.6,COL.stone);var p=L.P(q[0],.5,q[1]);mb.cyl(p,.16,.12,1.4,6,COL.trunk);mb.cyl([p[0],p[1]+1.1,p[2]],1.1,.5,1.3,7,COL.leaf,COL.leaf2);mb.cyl([p[0],p[1]+2.2,p[2]],.7,.06,1.1,7,COL.leaf2)});return}
  if(r.role==='garda'||r.role==='round'){L.b(mb,0,0,0,w-3,1.05,.8,COL.white);L.b(mb,0,0,1.05,w-2.8,.06,.9,COL.scrub);L.b(mb,-(w-3)/2,1.2,0,.8,1.05,1.8,COL.white);L.b(mb,(w-3)/2,1.2,0,.8,1.05,1.8,COL.white);
    chair(mb,L,-1,1,COL.dark);chair(mb,L,1,1,COL.dark);L.b(mb,-.8,0,1.11,.5,.36,.05,COL.screen);L.b(mb,.9,0,1.11,.5,.36,.05,COL.screen);shelf(mb,L,0,3.9,w-2,['white','c2','white','c0']);return}
  if(r.role==='library'){for(i=-1;i<=1;i++)shelf(mb,L,i*2.6,3.9,2.2,['c1','c3','c4','c0','c2']);shelf(mb,L,-w/2+.35,0,.5,['c1','c3']);for(i=-1;i<=1;i+=2){L.b(mb,i*1.8,0,.72,1.6,.06,1,COL.wood);L.b(mb,i*1.8,0,0,.2,.72,.2,COL.wood2);chair(mb,L,i*1.8,-.9,COL.c3);L.b(mb,i*1.8,.1,.78,.5,.04,.35,COL.white)}return}
  if(r.role==='boss'||r.role==='league'){L.b(mb,0,.5,.72,w-3,.07,1.5,COL.wood);L.b(mb,0,.5,0,w-4,.72,.4,COL.wood2);for(i=0;i<Math.floor(w-3);i++){chair(mb,L,-(w-3)/2+.5+i,1.7,COL.dark);chair(mb,L,-(w-3)/2+.5+i,-.7,COL.c1)}
    L.b(mb,0,4.1,1,3,1.4,.06,COL.screen);L.b(mb,0,4.05,1.1,2.8,1.2,.02,r.role==='league'?COL.yellow:COL.white);return}
  if(r.role==='exam'){for(i=0;i<4;i++)for(j=0;j<3;j++){L.b(mb,-3+i*2,2.5-j*1.8,.72,1,.05,.6,COL.wood);L.b(mb,-3+i*2,2.5-j*1.8,0,.1,.72,.1,COL.wood2);chair(mb,L,-3+i*2,1.9-j*1.8,COL.c4)}L.b(mb,0,4.1,1,4,1.4,.06,COL.screen);return}
  if(t==='waiting'){for(i=0;i<6;i++){chair(mb,L,-w/2+1+i*1,3.6,COL.c0);chair(mb,L,-w/2+1+i*1,.6,COL.c2)}cabinet(mb,L,awayU(r,w,2,1),-3.3,2);plant(mb,L,awayU(r,w,-w/2+.6,.45),-3.4);return}
  if(t==='terrace'){for(i=-1;i<=1;i+=2){L.b(mb,i*2.5,2,0,2,.5,.8,COL.pot);mb.cyl(L.P(i*2.5,.5,2),.9,.1,1.2,7,COL.plant)}L.b(mb,0,-1,.45,2.4,.08,.5,COL.wood);L.b(mb,0,-1,0,.2,.45,.4,COL.wood2);return}
  if(t==='pharmacy')return;
  /* default: consulting room / office */
  desk(mb,L,w/2-1.6,2.6);chair(mb,L,w/2-1.6,1,COL.c0);if(t==='exam'){L.b(mb,-w/2+1.1,2.2,0,.75,.65,1.9,COL.steel);L.b(mb,-w/2+1.1,2.2,.65,.8,.1,1.95,COL.sheet);L.b(mb,-w/2+1.1,3,.75,.5,.1,.35,COL.white)}
  cabinet(mb,L,-w/2+1.3,3.9,1.8);var pu=w/2-.6;if(awayU(r,w,pu,.45)!==pu)pu=-(w/2-1.4);plant(mb,L,pu,-3.4)}

function buildFloor(f){var s=new MB(),p=new MB(),segs=[];DOORS=DOORS.filter(function(d){return d.f!==f});buildCore(s,f);ROOMS.forEach(function(r){if(r.f!==f)return;var a=new MB(),b=new MB();buildRoomShell(s,a,b,r);furnish(p,r);
    if(a.count())segs.push({room:r.id,side:'s',ext:r.z1===BZ?1:0,x0:r.x0,x1:r.x1,z:r.z1,mb:a});if(b.count())segs.push({room:r.id,side:'n',ext:0,x0:r.x0,x1:r.x1,z:r.z0,mb:b})});return{shell:s,props:p,segs:segs,doors:DOORS.filter(function(d){return d.f===f})}}
function buildRoof(mb){var y=16,i,j;
  mb.box(0,y-.15,-(BZ+AZ)/2,2*BX+.4,.3,BZ-AZ+.2,COL.roof);mb.box(0,y-.15,(BZ+AZ)/2,2*BX+.4,.3,BZ-AZ+.2,COL.roof);[-1,1].forEach(function(q){mb.box(q*(BX+16.1)/2,y-.15,0,BX-15.9,.3,2*AZ,COL.roof);mb.box(q*12.5,y-.15,-2.5,7.2,.3,11,COL.roof)});
  wall(mb,'x',-BX,BX,-BZ,.2,y,.7,COL.wall);wall(mb,'x',-BX,BX,BZ,.2,y,.7,COL.wall);wall(mb,'z',-BZ,BZ,-BX,.2,y,.7,COL.wall);wall(mb,'z',-BZ,BZ,BX,.2,y,.7,COL.wall);
  mb.box(0,y+.74,-BZ,2*BX+.5,.08,.4,COL.trim);mb.box(0,y+.74,BZ,2*BX+.5,.08,.4,COL.trim);mb.box(-BX,y+.74,0,.4,.08,2*BZ+.3,COL.trim);mb.box(BX,y+.74,0,.4,.08,2*BZ+.3,COL.trim);
  /* glazed skylight over the atrium */
  wall(mb,'x',-AX,AX,-AZ,.25,y,.9,COL.wallx);wall(mb,'x',-AX,AX,AZ,.25,y,.9,COL.wallx);wall(mb,'z',-AZ,AZ,-AX,.25,y,.9,COL.wallx);wall(mb,'z',-AZ,AZ,AX,.25,y,.9,COL.wallx);
  mb.b(0,y+.9,0,2*AX+.3,.06,2*AZ+.3,COL.glass);for(i=-2;i<=2;i++)mb.b(i*3.6,y+.94,0,.12,.08,2*AZ+.3,COL.frame);for(i=-2;i<=2;i++)mb.b(0,y+.94,i*3.2,2*AX+.3,.08,.12,COL.frame);
  [[-21,-14],[-15,-14],[-9,-14],[-3,-14]].forEach(function(q){mb.b(q[0],y,q[1],3.2,1.3,2.2,COL.hvac);mb.b(q[0],y+1.3,q[1],2.6,.1,1.6,COL.roof2)});
  for(i=0;i<6;i++)for(j=0;j<2;j++){mb.b(-5.8+i*2.3,y+.25,12+j*3,2,.08,1.3,COL.solar);mb.b(-5.8+i*2.3,y,12+j*3,.1,.25,.1,COL.steel);mb.b(-5.8+i*2.3,y+.33,12+j*3,.04,.01,1.3,COL.steel)}
  mb.cyl([-22,y,13.5],1.4,1.4,2.2,12,COL.steel,COL.roof2);mb.b(-22.5,y+.2,0,.25,.25,20,COL.steel);mb.b(-14,y+.2,-10.6,18,.25,.25,COL.steel);mb.b(23,y,14,.1,5,.1,COL.steel);mb.b(23,y+3.6,14,1.2,.06,.06,COL.steel);mb.b(23,y+4.4,14,.8,.06,.06,COL.steel);
  for(i=0;i<8;i++)mb.b(20+4.2*Math.cos(i*TAU/8),y+.08,-13+4.2*Math.sin(i*TAU/8),.2,.12,.2,COL.yellow);
  mb.cyl([20,y,-13],4.5,4.5,.08,24,COL.roof2,COL.roof2);mb.b(18.8,y+.08,-13,.5,.03,3.4,COL.white);mb.b(21.2,y+.08,-13,.5,.03,3.4,COL.white);mb.b(20,y+.08,-13,2.2,.03,.5,COL.white)}
/* lift towers: hollow shafts, one continuous shell each from the ground to above the roof, no floors inside; door openings face south on G, 1, 2, 3 and the roof */
function buildLifts(mb){var top=19.2,th=.2;TOWERS.forEach(function(t){var x0=t.x0+.02,x1=t.x1-.02,z0=t.z0+.02,z1=t.z1-.02,cx=(x0+x1)/2,cz=(z0+z1)/2;
    wall(mb,'x',x0,x1,z0+th/2,th,-.25,top+.25,COL.wallx);wall(mb,'z',z0+th,z1-th,x0+th/2,th,-.25,top+.25,COL.wallx);wall(mb,'z',z0+th,z1-th,x1-th/2,th,-.25,top+.25,COL.wallx);
    wall(mb,'x',x0,x1,z1-th/2,th,-.25,.25,COL.wallx);
    for(var f=0;f<5;f++){var y=f*FH,h=f<4?FH:top-y;wall(mb,'x',x0,x1,z1-th/2,th,y,h,COL.wallx,t.lifts.map(function(lx){return{c:lx,w:1.7,sill:0,top:2.3}}));
      t.lifts.forEach(function(lx){mb.b(lx-.9,y,z1+.02,.1,2.3,.12,COL.steel);mb.b(lx+.9,y,z1+.02,.1,2.3,.12,COL.steel);mb.b(lx,y+2.3,z1+.02,1.9,.1,.12,COL.steel);mb.box(lx,y+2.62,z1+.03,.5,.2,.04,COL.yellow);mb.box(lx+1.15,y+1.2,z1+.03,.1,.22,.04,COL.dark)})}
    mb.box(cx,-.2,cz,x1-x0-.3,.1,z1-z0-.3,COL.dark);                                                        /* pit floor */
    t.lifts.forEach(function(lx){[-1.25,1.25].forEach(function(q){mb.b(lx+q,-.25,z0+th+.08,.08,top,.08,COL.steel)})});   /* guide rails */
    mb.b(cx,top,cz,x1-x0+.3,.25,z1-z0+.3,COL.trim);mb.b(cx,top+.25,cz,.4,.4,.4,COL.red)})}
/* one lift car, open-topped so the camera sees inside; origin = centre of its floor, doorway on +z */
function buildCab(mb){mb.b(0,0,0,2.3,.12,2.9,COL.steel);mb.b(0,.12,0,2.1,.02,2.7,COL.f_or);mb.b(0,.12,-1.4,2.3,2.3,.08,COL.white);mb.b(-1.11,.12,0,.08,2.3,2.9,COL.white);mb.b(1.11,.12,0,.08,2.3,2.9,COL.white);
  mb.b(0,1.0,-1.33,2,.06,.06,COL.steel);mb.b(-1.04,1.0,0,.06,.06,2.5,COL.steel);mb.b(1.04,1.0,0,.06,.06,2.5,COL.steel);mb.b(0,1.3,-1.35,1.4,.8,.03,COL.glass);
  mb.b(.9,1,1.3,.08,.5,.3,COL.dark);mb.b(.9,1.15,1.3,.1,.12,.14,COL.yellow);mb.b(-.95,.12,1.4,.4,2.3,.08,COL.white);mb.b(.95,.12,1.4,.4,2.3,.08,COL.white)}
function tree(mb,x,z,s){mb.cyl([x,0,z],.3*s,.22*s,1.6*s,6,COL.trunk);mb.cyl([x,1.3*s,z],2*s,.9*s,2*s,7,COL.leaf,COL.leaf2);mb.cyl([x,3*s,z],1.3*s,.1,1.8*s,7,COL.leaf2)}
function buildSite(mb){var i,S=BZ;mb.box(0,-.4,3,110,.3,86,COL.grass);
  mb.b(0,-.24,37.5,72,.05,5,COL.road);mb.b(-4,-.24,S+9.6,4,.05,14.8,COL.road);mb.b(4,-.24,S+9.6,4,.05,14.8,COL.road);mb.b(41.5,-.24,14.5,27,.05,3,COL.road);mb.b(0,-.24,S+3.6,14,.09,2.8,COL.pave);
  mb.b(-33,-.24,28,38,.05,10,COL.road);for(i=0;i<12;i++)mb.b(-51.5+i*3.2,-.18,28,.12,.02,9,COL.line);
  var cc=['car1','car2','car4','car3','car5','car2','car1','car4'];[0,1,3,4,6,7,9,10].forEach(function(b,k){var x=-49.9+b*3.2,z=28+(k%2?.3:-.2);mb.b(x,-.05,z,1.8,.65,4.2,COL[cc[k]]);mb.b(x,.6,z+.2,1.6,.55,2.2,COL.glass);mb.b(x,1.15,z+.2,1.62,.05,2.1,COL[cc[k]]);
    [[-.9,-1.4],[.9,-1.4],[-.9,1.4],[.9,1.4]].forEach(function(q){mb.box(x+q[0],.1,z+q[1],.22,.62,.62,COL.tyre)});mb.box(x-.55,.35,z-2.11,.3,.14,.03,COL.yellow);mb.box(x+.55,.35,z-2.11,.3,.14,.03,COL.yellow)});
  for(i=0;i<10;i++)mb.b(-28+i*6.2,-.2,37.5,2.4,.02,.16,COL.line);for(i=0;i<5;i++){mb.b(-5.4+i*.7,-.2,33.6,.4,.02,2.6,COL.line);mb.b(2.6+i*.7,-.2,33.6,.4,.02,2.6,COL.line)}
  [[-17.5,S+3.2,17],[17.5,S+3.2,17],[-14,-S-2.4,24],[14,-S-2.4,24]].forEach(function(q){mb.b(q[0],-.2,q[1],q[2],.9,.9,COL.hedge)});
  [[-9.5,S+2.6],[9.5,S+2.6]].forEach(function(q){mb.b(q[0],-.24,q[1],4,.3,2.4,COL.stone);mb.b(q[0],.06,q[1],3.6,.08,2,COL.trunk);for(var k=0;k<10;k++)mb.cyl([q[0]-1.5+(k%5)*.75,.1,q[1]-.5+Math.floor(k/5)],.22,.02,.35,5,COL[k%2?'flower':'flower2'])});
  for(i=0;i<11;i++){var lx=-32+i*6.4;mb.cyl([lx,-.25,33.5],.1,.07,4.2,6,COL.frame);mb.b(lx,3.9,33.9,.14,.08,1,COL.frame);mb.b(lx,3.78,34.3,.3,.12,.5,COL.yellow)}
  [-3,-1.8,1.8,3].forEach(function(bx){mb.cyl([bx,-.24,S+6],.12,.12,.8,6,COL.yellow,COL.dark)});
  [[-13,S+5.5],[13,S+5.5],[-34,-25],[-31,-27]].forEach(function(q){mb.b(q[0],.2,q[1],1.8,.08,.5,COL.wood);mb.b(q[0],.28,q[1]-.22,1.8,.45,.06,COL.wood);mb.b(q[0]-.8,-.24,q[1],.08,.45,.45,COL.frame);mb.b(q[0]+.8,-.24,q[1],.08,.45,.45,COL.frame)});
  mb.cyl([-40,-.24,-31],5,5,.06,18,COL.water);for(i=0;i<14;i++){var an=i/14*TAU;mb.b(-40+5.3*Math.cos(an),-.24,-31+5.3*Math.sin(an),.7,.25,.7,COL.stone)}for(i=0;i<5;i++)mb.cyl([-43+i*.4,-.2,-28.2+i*.2],.06,.02,1.1,4,COL.leaf);
  mb.b(-37,-.24,-24,14,.04,1.6,COL.pave);mb.b(-29.5,-.24,-22,1.6,.04,5,COL.pave);
  for(i=0;i<8;i++)tree(mb,-50+i*8,-37,1+.15*(i%3));[[40,26],[45,30],[50,26],[-52,-14],[-46,-20],[48,-32],[38,-35],[46,-6],[50,2]].forEach(function(q,j){tree(mb,q[0],q[1],.9+.2*(j%2))});
  [[30,22],[34,22],[38,22],[-30,-34],[-12,-36],[10,-35],[24,-33],[-48,40],[48,40]].forEach(function(q){mb.cyl([q[0],-.24,q[1]],1.1,.5,1.1,7,COL.leaf2,COL.leaf)});
  mb.b(26,-.24,40.8,5,.1,1.8,COL.pave);mb.b(26,2.2,41.2,5,.1,1.4,COL.frame);mb.b(26,-.2,41.8,5,2.4,.06,COL.glass);mb.b(23.6,-.2,41.2,.1,2.4,.1,COL.frame);mb.b(28.4,-.2,41.2,.1,2.4,.1,COL.frame);mb.b(26,.2,41.3,3,.08,.5,COL.wood);
  for(i=0;i<5;i++){mb.b(8+i*.7,-.24,S+3.5,.05,.8,.05,COL.steel);mb.b(8+i*.7,.5,S+3.9,.05,.05,.85,COL.steel)}
  [[-7,S+6],[7,S+6]].forEach(function(q,k){mb.cyl([q[0],-.24,q[1]],.08,.06,7,6,COL.steel);mb.b(q[0]+.75,5.6,q[1],1.4,.9,.03,COL[k?'c0':'red'])});
  for(i=-54;i<=54;i+=6){mb.b(i,-.25,-40,.3,1.6,.3,COL.stone);mb.b(i,-.25,46,.3,1.6,.3,COL.stone)}for(i=-40;i<=46;i+=6.2){mb.b(-55,-.25,i,.3,1.6,.3,COL.stone);mb.b(55,-.25,i,.3,1.6,.3,COL.stone)}
  wall(mb,'x',-55,55,-40,.15,-.25,1.3,COL.frame);wall(mb,'x',-55,55,46,.15,-.25,1.3,COL.frame,[{c:0,w:8,sill:0,top:1.3}]);
  wall(mb,'z',-40,46,-55,.15,-.25,1.3,COL.frame,[{c:28,w:4,sill:0,top:1.3}]);wall(mb,'z',-40,46,55,.15,-.25,1.3,COL.frame,[{c:14.5,w:6,sill:0,top:1.3}]);
  /* parked ambulance by the emergency entrance */
  mb.b(34,-.2,14.6,5,1.9,2.2,COL.white);mb.b(36.9,-.2,14.6,1.2,1.3,2.1,COL.white);mb.box(33.5,1,15.72,1,.3,.04,COL.red);mb.box(33.5,1,15.72,.3,1,.04,COL.red);mb.b(34,1.7,14.6,.5,.2,1.4,COL.blue)}
/* exterior model of the institute for the island map, built from the same room data as the playable building (windows sit where the rooms put them).
   1 unit = 4 m: footprint 13 x 9 units on a 16 x 13 paved plot, 4.9 units high at the lift towers. Origin = ground centre, entrance faces +z. */
function buildIslandModel(mb){var S=.25,i,j;
  function B(x,y0,z,sx,h,sz,c){mb.b(x*S,y0*S,z*S,sx*S,h*S,sz*S,c)}
  function C(x,y0,z,r0,r1,h,n,c,c2){mb.cyl([x*S,y0*S,z*S],r0*S,r1*S,h*S,n,c,c2)}
  B(0,-.3,2.5,64,.3,53,COL.pave);B(0,0,BZ+1.1,56,.12,2.2,COL.curb);B(0,0,-BZ-1,56,.12,2,COL.curb);B(-BX-1,0,0,2,.12,2*BZ,COL.curb);B(BX+1,0,0,2,.12,2*BZ,COL.curb);
  /* body: one block per storey, a trim band at each slab, brick base, corner piers */
  for(i=0;i<4;i++){B(0,i*4,0,2*BX,4,2*BZ,COL.wall);if(i)B(0,i*4-.3,0,2*BX+.3,.34,2*BZ+.3,COL.trim)}
  B(-14.1,0,BZ+.06,23.8,1.1,.14,COL.brick);B(14.1,0,BZ+.06,23.8,1.1,.14,COL.brick);B(0,0,-BZ-.06,2*BX+.2,1.1,.14,COL.brick);B(-BX-.06,0,0,.14,1.1,2*BZ,COL.brick);B(BX+.06,0,-5.5,.14,1.1,25,COL.brick);B(BX+.06,0,16.9,.14,1.1,2.2,COL.brick);
  [[-BX,-BZ],[BX,-BZ],[-BX,BZ],[BX,BZ]].forEach(function(q){B(q[0]+(q[0]<0?-.08:.08),0,q[1]+(q[1]<0?-.08:.08),.6,16,.6,COL.wallx)});
  /* windows exactly where the rooms have them */
  ROOMS.forEach(function(r){if(r.open||r.type==='toilet'||r.type==='terrace'||(r.type==='store'&&!r.locker))return;var y=r.f*4;
    [['N',r.z0===-BZ,r.x0,r.x1,-BZ,0,-1],['S',r.z1===BZ,r.x0,r.x1,BZ,0,1],['W',r.x0===-BX,r.z0,r.z1,-BX,-1,0],['E',r.x1===BX,r.z0,r.z1,BX,1,0]].forEach(function(e){if(!e[1])return;
      if((r.entrance&&e[0]==='S')||(r.ambulance&&e[0]==='E'))return;var len=e[3]-e[2],n=Math.max(1,Math.round(len/5.5)),ww=Math.min(3.2,len/n-1.6),ns=e[5]===0;
      for(var k=0;k<n;k++){var c=e[2]+len*(k+.5)/n,o=e[4]+(e[5]+e[6])*.07,o2=e[4]+(e[5]+e[6])*.12;
        if(ns){B(c,y+.9,o,ww,1.6,.1,COL.glass);B(c,y+.9,o2,.12,1.6,.08,COL.frame);B(c,y+1.66,o2,ww,.1,.08,COL.frame);B(c,y+.78,o2,ww+.3,.12,.3,COL.white);B(c,y+2.5,o2,ww+.2,.14,.2,COL.trim)}
        else{B(o,y+.9,c,.1,1.6,ww,COL.glass);B(o2,y+.9,c,.08,1.6,.12,COL.frame);B(o2,y+1.66,c,.08,.1,ww,COL.frame);B(o2,y+.78,c,.3,.12,ww+.3,COL.white);B(o2,y+2.5,c,.2,.14,ww+.2,COL.trim)}}})});
  /* third-floor terrace: an open loggia at the south-east corner */
  B(21.5,12.2,BZ+.02,9,2.6,.3,COL.dark);B(BX+.02,12.2,14.5,.3,2.6,7,COL.dark);B(21.5,12,BZ+.1,9,1.1,.12,COL.glass);B(BX+.1,12,14.5,.12,1.1,7,COL.glass);C(20,12.2,BZ-.6,.5,.1,1.4,6,COL.plant);C(24,12.2,BZ-.6,.5,.1,1.4,6,COL.plant);
  /* main entrance: glass doors, canopy on two columns, sign band, red cross */
  B(0,0,BZ+.08,4.4,2.8,.14,COL.glass);B(-2.3,0,BZ+.1,.2,2.9,.2,COL.frame);B(2.3,0,BZ+.1,.2,2.9,.2,COL.frame);B(0,0,BZ+.12,.14,2.8,.1,COL.frame);
  B(0,2.8,BZ+1.8,11,.3,3.5,COL.white);B(-5.1,0,BZ+3,.34,2.8,.34,COL.steel);B(5.1,0,BZ+3,.34,2.8,.34,COL.steel);
  B(0,3.1,BZ+3.3,10.6,1.25,.22,COL.blue);B(0,3.55,BZ+3.44,9,.34,.06,COL.white);
  [-4.8,-1.9,1.9,4.8].forEach(function(q){B(q,3.1,BZ+3.1,.2,1.25,.2,COL.steel);B(q,3.1,BZ+2.4,.2,.65,.2,COL.steel);B(q,3.65,BZ+2.75,.14,.14,.9,COL.steel)});
  B(0,3.1,BZ+1.8,1.9,.3,1.1,COL.white);B(0,4.5,BZ+1.8,3.4,1.2,.6,COL.red);B(0,3.4,BZ+1.8,1.2,3.4,.6,COL.red);
  /* emergency entrance on the east side, with its own canopy and a parked ambulance */
  B(BX+.08,0,14.5,.14,2.4,2.4,COL.dark);B(BX+1.2,2.5,14.5,2.4,.2,3.4,COL.red);B(BX+2.2,0,13,.2,2.5,.2,COL.steel);B(BX+2.2,0,16,.2,2.5,.2,COL.steel);
  B(BX+4,0,20.5,2.2,1.9,5,COL.white);B(BX+4,0,23.6,2.1,1.3,1.2,COL.white);B(BX+4,1.9,20.5,1.4,.2,.5,COL.blue);B(BX+5.13,.8,20,.04,.3,1,COL.red);B(BX+5.13,.45,20,.04,1,.3,COL.red);
  [[-.9,19],[.9,19],[-.9,22.6],[.9,22.6]].forEach(function(q){B(BX+4+q[0]*1.15,0,q[1],.25,.7,.7,COL.tyre)});
  /* roof: parapet with coping, glazed atrium skylight, two lift towers, plant, solar panels, helipad */
  B(0,16,0,2*BX+.4,.3,2*BZ+.4,COL.roof);B(0,16.3,-BZ,2*BX+.4,.7,.3,COL.wall);B(0,16.3,BZ,2*BX+.4,.7,.3,COL.wall);B(-BX,16.3,0,.3,.7,2*BZ,COL.wall);B(BX,16.3,0,.3,.7,2*BZ,COL.wall);
  B(0,17,-BZ,2*BX+.6,.1,.5,COL.trim);B(0,17,BZ,2*BX+.6,.1,.5,COL.trim);B(-BX,17,0,.5,.1,2*BZ+.4,COL.trim);B(BX,17,0,.5,.1,2*BZ+.4,COL.trim);
  B(0,16.3,-AZ,2*AX+.4,.9,.35,COL.wallx);B(0,16.3,AZ,2*AX+.4,.9,.35,COL.wallx);B(-AX,16.3,0,.35,.9,2*AZ,COL.wallx);B(AX,16.3,0,.35,.9,2*AZ,COL.wallx);
  B(0,17.2,0,2*AX+.4,.1,2*AZ+.4,COL.glass);for(i=-2;i<=2;i++){B(i*3.6,17.28,0,.18,.1,2*AZ+.4,COL.frame);B(0,17.28,i*3.2,2*AX+.4,.1,.18,COL.frame)}
  TOWERS.forEach(function(t){var cx=(t.x0+t.x1)/2,cz=(t.z0+t.z1)/2;B(cx,16.3,cz,t.x1-t.x0,2.9,t.z1-t.z0,COL.wallx);B(cx,19.2,cz,t.x1-t.x0+.4,.25,t.z1-t.z0+.4,COL.trim);B(cx,19.45,cz,.5,.45,.5,COL.red);
    t.lifts.forEach(function(lx){B(lx,16.3,t.z1+.06,1.7,2.3,.1,COL.steel);B(lx,18.75,t.z1+.06,.5,.2,.1,COL.yellow)})});
  [[-21,-14],[-15,-14],[-9,-14],[-3,-14]].forEach(function(q){B(q[0],16.3,q[1],3.2,1.3,2.2,COL.hvac);B(q[0],17.6,q[1],2.6,.12,1.6,COL.roof2)});
  for(i=0;i<6;i++)for(j=0;j<2;j++){B(-5.8+i*2.3,16.55,12+j*3,2,.1,1.3,COL.solar);B(-5.8+i*2.3,16.3,12+j*3,.14,.25,.14,COL.steel)}
  C(-22,16.3,13.5,1.4,1.4,2.2,10,COL.steel,COL.roof2);B(-22.5,16.5,0,.3,.3,20,COL.steel);B(-14,16.5,-10.6,18,.3,.3,COL.steel);B(23,16.3,14,.14,5,.14,COL.steel);B(23,19.9,14,1.2,.1,.1,COL.steel);B(23,20.7,14,.8,.1,.1,COL.steel);
  C(20,16.3,-13,4.5,4.5,.1,20,COL.roof2,COL.roof2);B(18.8,16.4,-13,.5,.05,3.4,COL.white);B(21.2,16.4,-13,.5,.05,3.4,COL.white);B(20,16.4,-13,2.2,.05,.5,COL.white);
  for(i=0;i<8;i++)B(20+4.2*Math.cos(i*TAU/8),16.4,-13+4.2*Math.sin(i*TAU/8),.3,.16,.3,COL.yellow);
  /* forecourt and planting */
  B(0,0,BZ+6.5,14,.08,8.5,COL.curb);B(-4,0,BZ+8.8,4,.05,4,COL.road);B(4,0,BZ+8.8,4,.05,4,COL.road);for(i=0;i<4;i++){B(-5.2+i*.8,.06,BZ+8.8,.4,.02,2.6,COL.line);B(2.8+i*.8,.06,BZ+8.8,.4,.02,2.6,COL.line)}
  [[-17.5,BZ+3.2,17],[17.5,BZ+3.2,17],[-14,-BZ-2.6,24],[14,-BZ-2.6,24]].forEach(function(q){B(q[0],0,q[1],q[2],.9,.9,COL.hedge)});
  [[-9.5,BZ+2.6],[9.5,BZ+2.6]].forEach(function(q){B(q[0],0,q[1],4,.35,2.4,COL.stone);for(var k=0;k<8;k++)C(q[0]-1.4+(k%4)*.9,.35,q[1]-.5+Math.floor(k/4),.3,.03,.5,5,COL[k%2?'flower':'flower2'])});
  [[-29,BZ+5],[-27,-BZ-5],[28,-BZ-5],[-30,2],[29.5,-6],[-22,BZ+7.2],[22,BZ+7.2]].forEach(function(q,k){var z=1+.2*(k%3);C(q[0],0,q[1],.35*z,.25*z,1.8*z,6,COL.trunk);C(q[0],1.5*z,q[1],2.2*z,1*z,2.2*z,7,COL.leaf,COL.leaf2);C(q[0],3.4*z,q[1],1.4*z,.1,2*z,7,COL.leaf2)});
  for(i=0;i<5;i++){var lx=-24+i*12;C(lx,0,BZ+7.6,.12,.09,4.2,5,COL.frame);B(lx,4,BZ+8,.18,.1,1,COL.frame);B(lx,3.85,BZ+8.4,.36,.14,.5,COL.yellow)}
  [-3,-1.8,1.8,3].forEach(function(bx){C(bx,0,BZ+5.6,.16,.16,.8,6,COL.yellow,COL.dark)});
  [[-13,BZ+5.2],[13,BZ+5.2]].forEach(function(q){B(q[0],.4,q[1],1.9,.1,.55,COL.wood);B(q[0],.5,q[1]-.24,1.9,.5,.08,COL.wood);B(q[0]-.8,0,q[1],.1,.4,.5,COL.frame);B(q[0]+.8,0,q[1],.1,.4,.5,COL.frame)});
  [[-7,BZ+6.2,'red'],[7,BZ+6.2,'c0']].forEach(function(q){C(q[0],0,q[1],.1,.07,7,5,COL.steel);B(q[0]+.8,5.7,q[1],1.5,.95,.05,COL[q[2]])})}
function f2(v){return Math.round(v*100)/100}
function mgmesh(groups){var pal=[],ix={},out=['# mgmesh 1 | y-up, map units | flat-shaded triangle soup, counter-clockwise = outward','# g <name> | c <index> <rrggbb> | t <colour index> x y z x y z x y z | i <instance line>'],body=[];
  groups.forEach(function(g){body.push('g '+g.name);(g.extra||[]).forEach(function(e){body.push(e)});
    for(var t=0;t<g.mb.h.length;t++){var h=g.mb.h[t];if(ix[h]===undefined){ix[h]=pal.length;pal.push(h)}var p=g.mb.p,o=t*9,l='t '+ix[h];for(var k=0;k<9;k++)l+=' '+f2(p[o+k]);body.push(l)}});
  pal.forEach(function(h,i){out.push('c '+i+' '+h)});return out.concat(body).join('\n')}
if(typeof module!=='undefined')module.exports={buildLeaf:buildLeaf,buildCab:buildCab,buildLifts:buildLifts,KIND_OF:KIND_OF,buildFloor:buildFloor,buildRoof:buildRoof,buildSite:buildSite,buildIslandModel:buildIslandModel,mgmesh:mgmesh,MB:MB};

