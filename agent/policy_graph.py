"""Build and visualize a small airline cancellation policy knowledge graph."""

from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
import networkx as nx


NODE_COLORS = {
    "Entity": "#2563eb",
    "Attribute": "#d97706",
    "Value": "#7c3aed",
    "Action": "#0891b2",
    "Condition": "#0f766e",
}

# Fixed positions make repeated renders easy to compare.
POSITIONS = {
    "Passenger": (-4.0, 3.5),
    "LookupReservation": (-2.0, 3.5),
    "CancelReservation": (0.4, 3.5),
    "Reservation": (3.0, 3.5),
    "ReservationDatabase": (5.3, 3.5),
    "Exists": (-4.1, 1.5),
    "IsConfirmed": (-2.0, 1.5),
    "IsRefundable": (0.2, 1.5),
    "DepartureBeyond24Hours": (2.5, 1.5),
    "HasAuthorization": (5.1, 1.5),
    "ReservationID": (-3.5, -0.6),
    "ReservationStatus": (-1.0, -0.6),
    "Refundable": (1.55, -0.6),
    "HoursUntilDeparture": (4.2, -0.6),
    "Confirmed": (-1.9, -2.7),
    "Cancelled": (0.2, -2.7),
}

DISPLAY_NAMES = {
    "ReservationDatabase": "Reservation\nDatabase",
    "CancelReservation": "Cancel\nReservation",
    "LookupReservation": "Lookup\nReservation",
    "ReservationStatus": "Reservation\nStatus",
    "HoursUntilDeparture": "Hours Until\nDeparture",
    "DepartureBeyond24Hours": "Departure > 24h",
    "HasAuthorization": "Has\nAuthorization",
    "ReservationID": "Reservation ID",
    "IsConfirmed": "Is Confirmed",
    "IsRefundable": "Is Refundable",
}


def build_policy_graph() -> nx.MultiDiGraph:
    """Represent general policy rules, not individual passenger reservations."""
    graph = nx.MultiDiGraph(name="Airline cancellation policy")

    def add_node(node_id: str, kind: str, description: str, **properties: object) -> None:
        graph.add_node(node_id, type=kind, description=description, **properties)

    def add_relation(source: str, target: str, relation: str, **properties: object) -> None:
        graph.add_edge(source, target, relation=relation, **properties)

    # Domain concepts and reservation attributes.
    add_node("Reservation", "Entity", "Airline booking")
    add_node("Passenger", "Entity", "Passenger who can authorize cancellation")
    add_node("ReservationDatabase", "Entity", "Authoritative reservation records")

    for node_id, description, data_type in [
        ("ReservationStatus", "Current reservation status", "enum"),
        ("Refundable", "Refund eligibility flag", "bool"),
        ("HoursUntilDeparture", "Time remaining before departure", "number"),
        ("ReservationID", "Unique reservation identifier", "string"),
    ]:
        add_node(node_id, "Attribute", description, data_type=data_type)

    add_node("Confirmed", "Value", "Active reservation", value="confirmed")
    add_node("Cancelled", "Value", "Cancelled reservation", value="cancelled")
    add_node("LookupReservation", "Action", "Retrieve a reservation")
    add_node("CancelReservation", "Action", "Cancel a reservation")

    # Conditions are evaluated against reservation data and the conversation state.
    add_node("Exists", "Condition", "Reservation exists", predicate="exists(reservation_id)")
    add_node("IsConfirmed", "Condition", "Reservation is confirmed",
             attribute="ReservationStatus", operator="==", value="confirmed")
    add_node("IsRefundable", "Condition", "Reservation is refundable",
             attribute="Refundable", operator="==", value=True)
    add_node("DepartureBeyond24Hours", "Condition", "Departure is more than 24 hours away",
             attribute="HoursUntilDeparture", operator=">", value=24)
    add_node("HasAuthorization", "Condition", "Passenger explicitly confirmed this cancellation",
             predicate="explicit_confirmation(reservation_id)")

    for attribute in ("ReservationStatus", "Refundable", "HoursUntilDeparture", "ReservationID"):
        add_relation("Reservation", attribute, "HAS_ATTRIBUTE")
    add_relation("Reservation", "ReservationDatabase", "STORED_IN")
    add_relation("ReservationStatus", "Confirmed", "HAS_VALUE")
    add_relation("ReservationStatus", "Cancelled", "HAS_VALUE")
    add_relation("Confirmed", "Cancelled", "TRANSITIONS_TO", action="CancelReservation")

    for action in ("LookupReservation", "CancelReservation"):
        add_relation(action, "Reservation", "OPERATES_ON")
    add_relation("LookupReservation", "Exists", "REQUIRES")

    requirements = {
        "Exists": "ReservationID",
        "IsConfirmed": "ReservationStatus",
        "IsRefundable": "Refundable",
        "DepartureBeyond24Hours": "HoursUntilDeparture",
        "HasAuthorization": "ReservationID",
    }
    for condition, attribute in requirements.items():
        add_relation("CancelReservation", condition, "REQUIRES")
        add_relation(condition, attribute, "CHECKS")

    # This describes who may authorize; it does not claim authorization occurred.
    add_relation("Passenger", "CancelReservation", "AUTHORIZES",
                 condition="HasAuthorization")
    return graph


def draw_policy_graph(graph: nx.MultiDiGraph, output_path: Path) -> None:
    """Save a stable, readable rendering of the policy graph."""
    edge_colors = {
        "REQUIRES": "#64748b", "CHECKS": "#0f766e",
        "HAS_ATTRIBUTE": "#d97706", "OPERATES_ON": "#0891b2",
        "HAS_VALUE": "#7c3aed", "TRANSITIONS_TO": "#7c3aed",
        "AUTHORIZES": "#64748b", "STORED_IN": "#2563eb",
    }
    fig, ax = plt.subplots(figsize=(18, 10), dpi=170)

    for source, target, data in graph.edges(data=True):
        nx.draw_networkx_edges(
            graph, POSITIONS, edgelist=[(source, target)], ax=ax,
            edge_color=edge_colors[data["relation"]], arrows=True,
            arrowstyle="-|>", arrowsize=16, width=1.55, alpha=0.55,
            node_size=2100, connectionstyle="arc3,rad=0.08",
        )

    nx.draw_networkx_nodes(
        graph, POSITIONS, ax=ax, node_size=2600,
        node_color=[NODE_COLORS[graph.nodes[node]["type"]] for node in graph],
        edgecolors="white", linewidths=1.8,
    )
    nx.draw_networkx_labels(
        graph, POSITIONS, ax=ax,
        labels={node: DISPLAY_NAMES.get(node, node) for node in graph},
        font_color="white", font_size=8.5, font_weight="bold",
    )

    # Label only the critical policy relationships to avoid visual clutter.
    edge_labels = {
        ("CancelReservation", condition): "REQUIRES"
        for condition in (
            "Exists", "IsConfirmed", "IsRefundable",
            "DepartureBeyond24Hours", "HasAuthorization",
        )
    }
    edge_labels[("Confirmed", "Cancelled")] = "TRANSITIONS_TO"
    nx.draw_networkx_edge_labels(
        graph, POSITIONS, edge_labels=edge_labels, ax=ax,
        font_size=7, font_color="#334155", rotate=False,
        bbox={"facecolor": "white", "edgecolor": "none", "alpha": 0.8, "pad": 0.5},
    )

    legend = [
        Line2D([0], [0], marker="o", linestyle="None", markerfacecolor=color,
               markeredgecolor="none", markersize=10, label=kind)
        for kind, color in NODE_COLORS.items()
    ]
    ax.legend(handles=legend, loc="lower right", ncol=5, fontsize=10,
              frameon=False, bbox_to_anchor=(1, 0.01))
    ax.set_title("Airline Cancellation Policy — Knowledge Graph",
                 fontweight="bold", fontsize=20, pad=20)
    ax.text(0.5, 0.965,
            "Directed, typed edges • policy rules only • evaluated against runtime state",
            transform=ax.transAxes, ha="center", fontsize=10, color="#475569")
    ax.set_xlim(-5.4, 6.5)
    ax.set_ylim(-3.6, 4.5)
    ax.axis("off")
    fig.tight_layout()
    fig.savefig(output_path, bbox_inches="tight", facecolor="white")
    plt.close(fig)


def main() -> None:
    output_dir = Path(__file__).resolve().parent
    graph = build_policy_graph()
    draw_policy_graph(graph, output_dir / "policy_graph.png")
    nx.write_graphml(graph, output_dir / "policy_graph.graphml")
    print(f"Saved {graph.number_of_nodes()} nodes and {graph.number_of_edges()} edges")


if __name__ == "__main__":
    main()

