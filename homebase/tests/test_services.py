from datetime import date, datetime, timezone

from app.models import CarStatus
from app.services import car_classes as car_classes_service
from app.services import car_import as car_import_service
from app.services import cars as cars_service
from app.services import criteria as criteria_service
from app.services import shows as shows_service
from app.services.dashboard import get_summary


def _make_show(db_session):
    return shows_service.create_show(db_session, "Fall Cruise-In", date(2026, 9, 12), "Downtown Lot")


# ---------------------------------------------------------------------------
# Active show
# ---------------------------------------------------------------------------


def test_first_show_created_becomes_active(db_session):
    assert shows_service.get_active_show(db_session) is None
    show = _make_show(db_session)
    assert shows_service.get_active_show(db_session).id == show.id


def test_switching_active_show(db_session):
    show_a = _make_show(db_session)
    show_b = shows_service.create_show(db_session, "Spring Show", date(2027, 4, 1), "Fairgrounds")

    assert shows_service.get_active_show(db_session).id == show_a.id
    shows_service.set_active_show(db_session, show_b.id)
    assert shows_service.get_active_show(db_session).id == show_b.id


# ---------------------------------------------------------------------------
# Car Classes — delete reassigns cars, never orphans or cascade-deletes
# ---------------------------------------------------------------------------


def test_delete_class_reassigns_cars_to_unclassified(db_session):
    show = _make_show(db_session)
    car_class = car_classes_service.create_class(db_session, show.id, "1970s Muscle")
    car = cars_service.create_car(db_session, show.id, "0142", "142", "Chevrolet", "Camaro", 1967, car_class.id)

    reassigned = car_classes_service.delete_class(db_session, car_class.id)

    assert reassigned == 1
    db_session.refresh(car)
    assert car.class_id is None  # reassigned to Unclassified, not deleted


def test_move_class_swaps_sort_order(db_session):
    show = _make_show(db_session)
    first = car_classes_service.create_class(db_session, show.id, "Trucks")
    second = car_classes_service.create_class(db_session, show.id, "Stock")

    car_classes_service.move_class(db_session, show.id, second.id, "up")

    ordered = [c for c, _ in car_classes_service.list_classes_with_counts(db_session, show.id)]
    assert [c.id for c in ordered] == [second.id, first.id]


# ---------------------------------------------------------------------------
# Judging Criteria — deactivating must preserve historical scores
# ---------------------------------------------------------------------------


def test_deactivate_criteria_preserves_historical_scores(db_session):
    from app.models import Handheld, JudgingScore, JudgingSubmission

    show = _make_show(db_session)
    criteria = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    car = cars_service.create_car(db_session, show.id, "0142", "142", "Chevrolet", "Camaro", 1967, None)
    handheld = Handheld(label="Judge 1")
    db_session.add(handheld)
    db_session.commit()

    submission = JudgingSubmission(
        show_id=show.id,
        car_id=car.id,
        registration_number=car.registration_number,
        handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc),
    )
    db_session.add(submission)
    db_session.commit()
    score = JudgingScore(submission_id=submission.id, criteria_id=criteria.id, points=22)
    db_session.add(score)
    db_session.commit()
    score_id = score.id

    criteria_service.set_active(db_session, criteria.id, False)

    db_session.refresh(criteria)
    assert criteria.active is False
    # The score must still exist, untouched, and still resolvable to its criterion.
    still_there = db_session.get(JudgingScore, score_id)
    assert still_there is not None
    assert still_there.points == 22
    assert still_there.criteria.name == "Paint"


# ---------------------------------------------------------------------------
# Car deletion — cascades judging data, doesn't orphan rows
# ---------------------------------------------------------------------------


def test_delete_car_cascades_submissions_and_scores(db_session):
    from app.models import Handheld, JudgingScore, JudgingSubmission

    show = _make_show(db_session)
    criteria = criteria_service.create_criteria(db_session, show.id, "Paint", 25)
    car = cars_service.create_car(db_session, show.id, "0142", "142", "Chevrolet", "Camaro", 1967, None)
    handheld = Handheld(label="Judge 1")
    db_session.add(handheld)
    db_session.commit()
    submission = JudgingSubmission(
        show_id=show.id,
        car_id=car.id,
        registration_number=car.registration_number,
        handheld_id=handheld.id,
        closed_at=datetime.now(timezone.utc),
    )
    db_session.add(submission)
    db_session.commit()
    score = JudgingScore(submission_id=submission.id, criteria_id=criteria.id, points=22)
    db_session.add(score)
    db_session.commit()
    submission_id, score_id, car_id = submission.id, score.id, car.id

    cars_service.delete_car(db_session, car_id)

    assert db_session.get(JudgingSubmission, submission_id) is None
    assert db_session.get(JudgingScore, score_id) is None


def test_delete_car_requires_confirmation_summary_reports_data(db_session):
    from app.models import Handheld, JudgingSubmission

    show = _make_show(db_session)
    car = cars_service.create_car(db_session, show.id, "0142", "142", "Chevrolet", "Camaro", 1967, None)
    handheld = Handheld(label="Judge 1")
    db_session.add(handheld)
    db_session.commit()
    db_session.add(
        JudgingSubmission(
            show_id=show.id,
            car_id=car.id,
            registration_number=car.registration_number,
            handheld_id=handheld.id,
            closed_at=datetime.now(timezone.utc),
        )
    )
    db_session.commit()

    summary = cars_service.judging_data_summary(db_session, car.id)
    assert summary.has_data is True
    assert summary.submission_count == 1


# ---------------------------------------------------------------------------
# Cars — duplicate registration numbers rejected
# ---------------------------------------------------------------------------


def test_create_car_rejects_duplicate_registration_number(db_session):
    show = _make_show(db_session)
    cars_service.create_car(db_session, show.id, "0142", "142", "Chevrolet", "Camaro", 1967, None)

    try:
        cars_service.create_car(db_session, show.id, "0142", "999", "Ford", "Mustang", 1965, None)
        assert False, "expected DuplicateRegistrationNumber"
    except cars_service.DuplicateRegistrationNumber as exc:
        assert "0142" in str(exc)


# ---------------------------------------------------------------------------
# CSV import
# ---------------------------------------------------------------------------


def test_csv_import_rejects_duplicate_registration_numbers_in_file(db_session):
    show = _make_show(db_session)
    csv_text = (
        "registration_number,car_number,make,model,year,class_name\n"
        "0142,142,Chevrolet,Camaro,1967,Muscle\n"
        "0142,143,Ford,Mustang,1965,Muscle\n"
    )
    preview = car_import_service.build_preview(db_session, csv_text)
    assert len(preview.error_rows) == 2
    assert all("Duplicate registration number" in " ".join(r.errors) for r in preview.error_rows)
    assert "row(s) 3" in preview.error_rows[0].errors[0]


def test_csv_import_rejects_registration_number_already_in_db(db_session):
    show = _make_show(db_session)
    cars_service.create_car(db_session, show.id, "0142", "142", "Chevrolet", "Camaro", 1967, None)

    csv_text = "registration_number,car_number,make,model,year,class_name\n0142,999,Ford,Mustang,1965,\n"
    preview = car_import_service.build_preview(db_session, csv_text)

    assert len(preview.error_rows) == 1
    assert "already exists" in preview.error_rows[0].errors[0]


def test_csv_import_creates_missing_class_case_insensitively(db_session):
    show = _make_show(db_session)
    existing = car_classes_service.create_class(db_session, show.id, "1970s Muscle")

    csv_text = (
        "registration_number,car_number,make,model,year,class_name\n"
        "0142,142,Chevrolet,Camaro,1967,1970s muscle\n"
        "0231,231,Ford,F-100,1955,Trucks\n"
    )
    summary = car_import_service.commit_import(db_session, show.id, csv_text)

    assert summary.imported_count == 2
    cars = cars_service.list_cars(db_session, show.id)
    car_0142 = next(c for c in cars if c.registration_number == "0142")
    car_0231 = next(c for c in cars if c.registration_number == "0231")

    assert car_0142.class_id == existing.id  # matched case-insensitively, not duplicated
    assert car_0231.car_class.name == "Trucks"  # created fresh


def test_csv_import_skips_invalid_rows_but_imports_valid_ones(db_session):
    show = _make_show(db_session)
    csv_text = (
        "registration_number,car_number,make,model,year,class_name\n"
        "0142,142,Chevrolet,Camaro,1967,\n"
        ",143,Ford,Mustang,1965,\n"  # missing registration number
    )
    summary = car_import_service.commit_import(db_session, show.id, csv_text)

    assert summary.imported_count == 1
    assert len(summary.skipped_rows) == 1
    assert get_summary(db_session, show.id).total_cars == 1
